#include "android_java.h"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include "log.h"

namespace nin {

namespace {
constexpr uint32_t kHandleBase = 0x70000000;
constexpr uint32_t kMethodBase = 0x7b000000;

char returnType(const std::string& sig) {
  size_t close = sig.find(')');
  char r = close == std::string::npos || close + 1 >= sig.size() ? 'V' : sig[close + 1];
  return r == '[' ? 'L' : r;
}
}  // namespace

uint32_t AndroidJavaHost::store(jobject ref, bool global) {
  if (!ref) return 0;
  uint32_t index;
  if (!freeSlots_.empty()) {
    index = freeSlots_.back();
    freeSlots_.pop_back();
  } else {
    index = uint32_t(slots_.size());
    slots_.emplace_back();
  }
  slots_[index] = {ref, global, true};
  uint32_t handle = kHandleBase + 4 * (index + 1);
  if (!global) locals_.push_back(handle);
  return handle;
}

uint32_t AndroidJavaHost::wrapLocal(jobject obj) { return store(obj, false); }

jobject AndroidJavaHost::unwrap(uint32_t handle) const {
  if (handle == 0) return nullptr;
  uint32_t index = (handle - kHandleBase) / 4 - 1;
  if (handle < kHandleBase + 4 || index >= slots_.size() || !slots_[index].used) {
    NIN_LOGE("engine used a stale or invalid JNI handle %#x", handle);
    return nullptr;
  }
  return slots_[index].ref;
}

void AndroidJavaHost::endCall() {
  // Local references die with the native frame that created them.
  for (uint32_t handle : locals_) {
    uint32_t index = (handle - kHandleBase) / 4 - 1;
    if (index < slots_.size() && slots_[index].used && !slots_[index].global) {
      slots_[index] = {};
      freeSlots_.push_back(index);
    }
  }
  locals_.clear();
}

void AndroidJavaHost::releaseGlobals() {
  for (Slot& slot : slots_)
    if (slot.used && slot.global) env_->DeleteGlobalRef(slot.ref);
  slots_.clear();
  freeSlots_.clear();
  locals_.clear();
  classes_.clear();
}

void AndroidJavaHost::clearPendingException(const char* where) {
  if (env_->ExceptionCheck()) {
    NIN_LOGW("Java exception during %s (cleared)", where);
    env_->ExceptionDescribe();
    env_->ExceptionClear();
  }
}

uint32_t AndroidJavaHost::findClass(const std::string& name) {
  auto it = classes_.find(name);
  if (it != classes_.end()) return it->second;
  jclass local = env_->FindClass(name.c_str());
  if (!local) {
    clearPendingException("FindClass");
    return 0;
  }
  uint32_t handle = store(env_->NewGlobalRef(local), true);
  env_->DeleteLocalRef(local);
  classes_[name] = handle;
  return handle;
}

uint32_t AndroidJavaHost::methodId(uint32_t cls, const std::string& name, const std::string& sig, bool is_static) {
  auto c = static_cast<jclass>(unwrap(cls));
  jmethodID id = is_static ? env_->GetStaticMethodID(c, name.c_str(), sig.c_str())
                           : env_->GetMethodID(c, name.c_str(), sig.c_str());
  if (!id) {
    clearPendingException("GetMethodID");
    return 0;
  }
  auto it = methodIndex_.find(id);
  if (it != methodIndex_.end()) return it->second;
  methods_.push_back({id, sig, returnType(sig)});
  uint32_t guest = kMethodBase + uint32_t(methods_.size());
  methodIndex_[id] = guest;
  return guest;
}

const std::string& AndroidJavaHost::methodSignature(uint32_t method) {
  return methods_.at(method - kMethodBase - 1).sig;
}

JValue AndroidJavaHost::call(uint32_t method, uint32_t target, bool is_static, const std::vector<JValue>& args) {
  const Method& m = methods_.at(method - kMethodBase - 1);
  std::vector<jvalue> jargs(args.size());
  for (size_t i = 0; i < args.size(); i++) {
    const JValue& a = args[i];
    switch (a.type) {
      case 'Z': jargs[i].z = jboolean(a.i != 0); break;
      case 'B': jargs[i].b = jbyte(a.i); break;
      case 'C': jargs[i].c = jchar(a.i); break;
      case 'S': jargs[i].s = jshort(a.i); break;
      case 'I': jargs[i].i = a.i; break;
      case 'J': jargs[i].j = a.j; break;
      case 'F': jargs[i].f = a.f; break;
      case 'D': jargs[i].d = a.d; break;
      default: jargs[i].l = unwrap(a.obj);
    }
  }
  jobject t = unwrap(target);
  auto cls = static_cast<jclass>(t);
  JValue r;
  r.type = m.ret == 'L' ? 'L' : m.ret;
  switch (m.ret) {
    case 'V':
      is_static ? env_->CallStaticVoidMethodA(cls, m.id, jargs.data()) : env_->CallVoidMethodA(t, m.id, jargs.data());
      break;
    case 'Z':
      r.i = is_static ? env_->CallStaticBooleanMethodA(cls, m.id, jargs.data()) : env_->CallBooleanMethodA(t, m.id, jargs.data());
      break;
    case 'B':
      r.i = is_static ? env_->CallStaticByteMethodA(cls, m.id, jargs.data()) : env_->CallByteMethodA(t, m.id, jargs.data());
      break;
    case 'C':
      r.i = is_static ? env_->CallStaticCharMethodA(cls, m.id, jargs.data()) : env_->CallCharMethodA(t, m.id, jargs.data());
      break;
    case 'S':
      r.i = is_static ? env_->CallStaticShortMethodA(cls, m.id, jargs.data()) : env_->CallShortMethodA(t, m.id, jargs.data());
      break;
    case 'I':
      r.i = is_static ? env_->CallStaticIntMethodA(cls, m.id, jargs.data()) : env_->CallIntMethodA(t, m.id, jargs.data());
      break;
    case 'J':
      r.j = is_static ? env_->CallStaticLongMethodA(cls, m.id, jargs.data()) : env_->CallLongMethodA(t, m.id, jargs.data());
      break;
    case 'F':
      r.f = is_static ? env_->CallStaticFloatMethodA(cls, m.id, jargs.data()) : env_->CallFloatMethodA(t, m.id, jargs.data());
      break;
    case 'D':
      r.d = is_static ? env_->CallStaticDoubleMethodA(cls, m.id, jargs.data()) : env_->CallDoubleMethodA(t, m.id, jargs.data());
      break;
    default: {
      jobject o = is_static ? env_->CallStaticObjectMethodA(cls, m.id, jargs.data())
                            : env_->CallObjectMethodA(t, m.id, jargs.data());
      r.obj = store(o, false);
    }
  }
  clearPendingException("Java upcall");
  return r;
}

uint32_t AndroidJavaHost::newStringUtf(const std::string& s) { return store(env_->NewStringUTF(s.c_str()), false); }

std::string AndroidJavaHost::stringUtf(uint32_t str) {
  auto s = static_cast<jstring>(unwrap(str));
  if (!s) return {};
  const char* chars = env_->GetStringUTFChars(s, nullptr);
  std::string out(chars ? chars : "", size_t(env_->GetStringUTFLength(s)));
  if (chars) env_->ReleaseStringUTFChars(s, chars);
  return out;
}

std::u16string AndroidJavaHost::stringUtf16(uint32_t str) {
  auto s = static_cast<jstring>(unwrap(str));
  if (!s) return {};
  const jchar* chars = env_->GetStringChars(s, nullptr);
  std::u16string out(reinterpret_cast<const char16_t*>(chars), size_t(env_->GetStringLength(s)));
  env_->ReleaseStringChars(s, chars);
  return out;
}

uint32_t AndroidJavaHost::objectClass(uint32_t obj) { return store(env_->GetObjectClass(unwrap(obj)), false); }

void AndroidJavaHost::deleteLocalRef(uint32_t obj) {
  if (!obj) return;
  uint32_t index = (obj - kHandleBase) / 4 - 1;
  if (index >= slots_.size() || !slots_[index].used || slots_[index].global) return;
  env_->DeleteLocalRef(slots_[index].ref);
  slots_[index] = {};
  freeSlots_.push_back(index);
}

uint32_t AndroidJavaHost::newGlobalRef(uint32_t obj) {
  jobject o = unwrap(obj);
  return o ? store(env_->NewGlobalRef(o), true) : 0;
}

void AndroidJavaHost::deleteGlobalRef(uint32_t obj) {
  uint32_t index = (obj - kHandleBase) / 4 - 1;
  if (!obj || index >= slots_.size() || !slots_[index].used || !slots_[index].global) return;
  for (auto& [name, handle] : classes_)
    if (handle == obj) return;  // cached classes stay alive
  env_->DeleteGlobalRef(slots_[index].ref);
  slots_[index] = {};
  freeSlots_.push_back(index);
}

bool AndroidJavaHost::isSameObject(uint32_t a, uint32_t b) { return env_->IsSameObject(unwrap(a), unwrap(b)); }
bool AndroidJavaHost::exceptionCheck() { return env_->ExceptionCheck(); }
void AndroidJavaHost::exceptionClear() { env_->ExceptionClear(); }
void AndroidJavaHost::exceptionDescribe() { env_->ExceptionDescribe(); }

int32_t AndroidJavaHost::arrayLength(uint32_t array) {
  return env_->GetArrayLength(static_cast<jarray>(unwrap(array)));
}

bool AndroidJavaHost::readAsset(uint32_t asset_manager, const std::string& name, std::vector<uint8_t>& out) {
  AAssetManager* mgr = AAssetManager_fromJava(env_, unwrap(asset_manager));
  if (!mgr) return false;
  AAsset* asset = AAssetManager_open(mgr, name.c_str(), AASSET_MODE_BUFFER);
  if (!asset) return false;
  auto* data = static_cast<const uint8_t*>(AAsset_getBuffer(asset));
  size_t size = size_t(AAsset_getLength(asset));
  out.assign(data, data + size);
  AAsset_close(asset);
  return true;
}

}  // namespace nin

// Guest JNIEnv/JavaVM tables and the NDK asset-manager imports.
//
// The engine sees a JNIEnv at a fixed guest address whose 233 function
// pointers are stubs; calls are decoded (AAPCS, soft-float, varargs/va_list/
// jvalue[] by method signature) and forwarded to a JavaHost.
#include <algorithm>

#include "engine.h"
#include "log.h"

namespace nin {
namespace {

std::vector<std::string> jniFunctionNames() {
  std::vector<std::string> n = {
      "reserved0", "reserved1", "reserved2", "reserved3", "GetVersion", "DefineClass", "FindClass",
      "FromReflectedMethod", "FromReflectedField", "ToReflectedMethod", "GetSuperclass",
      "IsAssignableFrom", "ToReflectedField", "Throw", "ThrowNew", "ExceptionOccurred",
      "ExceptionDescribe", "ExceptionClear", "FatalError", "PushLocalFrame", "PopLocalFrame",
      "NewGlobalRef", "DeleteGlobalRef", "DeleteLocalRef", "IsSameObject", "NewLocalRef",
      "EnsureLocalCapacity", "AllocObject", "NewObject", "NewObjectV", "NewObjectA", "GetObjectClass",
      "IsInstanceOf", "GetMethodID"};
  const char* types[] = {"Object", "Boolean", "Byte", "Char", "Short", "Int", "Long", "Float", "Double", "Void"};
  for (auto t : types) for (auto s : {"", "V", "A"}) n.push_back(std::string("Call") + t + "Method" + s);
  for (auto t : types) for (auto s : {"", "V", "A"}) n.push_back(std::string("CallNonvirtual") + t + "Method" + s);
  n.push_back("GetFieldID");
  for (int i = 0; i < 9; i++) n.push_back(std::string("Get") + types[i] + "Field");
  for (int i = 0; i < 9; i++) n.push_back(std::string("Set") + types[i] + "Field");
  n.push_back("GetStaticMethodID");
  for (auto t : types) for (auto s : {"", "V", "A"}) n.push_back(std::string("CallStatic") + t + "Method" + s);
  n.push_back("GetStaticFieldID");
  for (int i = 0; i < 9; i++) n.push_back(std::string("GetStatic") + types[i] + "Field");
  for (int i = 0; i < 9; i++) n.push_back(std::string("SetStatic") + types[i] + "Field");
  for (auto s : {"NewString", "GetStringLength", "GetStringChars", "ReleaseStringChars", "NewStringUTF",
                 "GetStringUTFLength", "GetStringUTFChars", "ReleaseStringUTFChars", "GetArrayLength",
                 "NewObjectArray", "GetObjectArrayElement", "SetObjectArrayElement"})
    n.push_back(s);
  const char* prims[] = {"Boolean", "Byte", "Char", "Short", "Int", "Long", "Float", "Double"};
  for (auto p : prims) n.push_back(std::string("New") + p + "Array");
  for (auto p : prims) n.push_back(std::string("Get") + p + "ArrayElements");
  for (auto p : prims) n.push_back(std::string("Release") + p + "ArrayElements");
  for (auto p : prims) n.push_back(std::string("Get") + p + "ArrayRegion");
  for (auto p : prims) n.push_back(std::string("Set") + p + "ArrayRegion");
  for (auto s : {"RegisterNatives", "UnregisterNatives", "MonitorEnter", "MonitorExit", "GetJavaVM",
                 "GetStringRegion", "GetStringUTFRegion", "GetPrimitiveArrayCritical",
                 "ReleasePrimitiveArrayCritical", "GetStringCritical", "ReleaseStringCritical",
                 "NewWeakGlobalRef", "DeleteWeakGlobalRef", "ExceptionCheck", "NewDirectByteBuffer",
                 "GetDirectBufferAddress", "GetDirectBufferCapacity", "GetObjectRefType"})
    n.push_back(s);
  return n;
}

// "(ILjava/lang/String;F)V" -> ({"I","L","F"}, 'V'); arrays and objects -> 'L'.
void parseSignature(const std::string& sig, std::vector<char>& args, char& ret) {
  args.clear();
  size_t i = 1;
  while (i < sig.size() && sig[i] != ')') {
    size_t start = i;
    while (sig[i] == '[') i++;
    if (sig[i] == 'L') i = sig.find(';', i);
    i++;
    args.push_back(sig[start] == '[' || sig[start] == 'L' ? 'L' : sig[start]);
  }
  ret = i + 1 < sig.size() ? sig[i + 1] : 'V';
  if (ret == '[') ret = 'L';
}

// Modified UTF-8 <-> UTF-16 helpers for the string region functions.
std::string utf16ToModifiedUtf8(const std::u16string& s) {
  std::string out;
  for (char16_t c : s) {
    if (c != 0 && c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xc0 | (c >> 6));
      out += char(0x80 | (c & 0x3f));
    } else {
      out += char(0xe0 | (c >> 12));
      out += char(0x80 | ((c >> 6) & 0x3f));
      out += char(0x80 | (c & 0x3f));
    }
  }
  return out;
}

struct Jni {
  Engine& e;
  Guest& g;
  std::vector<std::string> names;
  uint32_t env = kJniArea, table = kJniArea + 0x100, stubs = kJniArea + 0x1000;
  uint32_t vm = kJniArea + 0x800, vmTable = kJniArea + 0x840, vmStubs = kJniArea + 0xf00;
  explicit Jni(Engine& engine) : e(engine), g(engine.guest()), names(jniFunctionNames()) {}

  std::vector<JValue> readArgs(uint32_t method, Args& a, char mode, char& ret) {
    std::vector<char> types;
    parseSignature(e.java().methodSignature(method), types, ret);
    std::vector<JValue> out(types.size());
    if (mode == 'A') {
      uint32_t base = a.u32();
      for (size_t k = 0; k < types.size(); k++) {
        uint32_t p = base + 8 * uint32_t(k);
        JValue& v = out[k];
        v.type = types[k];
        switch (types[k]) {
          case 'Z': v.i = *g.host(p); break;
          case 'B': v.i = int8_t(*g.host(p)); break;
          case 'C': v.i = g.r16(p); break;
          case 'S': v.i = int16_t(g.r16(p)); break;
          case 'I': v.i = int32_t(g.r32(p)); break;
          case 'J': std::memcpy(&v.j, g.host(p), 8); break;
          case 'F': v.f = g.rf32(p); break;
          case 'D': std::memcpy(&v.d, g.host(p), 8); break;
          default: v.obj = g.r32(p);
        }
      }
      return out;
    }
    Args va = mode == 'V' ? Args(g, a.u32(), true) : a;
    Args& src = mode == 'V' ? va : a;
    for (size_t k = 0; k < types.size(); k++) {
      JValue& v = out[k];
      v.type = types[k];
      switch (types[k]) {
        case 'J': v.j = src.i64(); break;
        case 'F': v.f = float(src.f64()); break;  // promoted to double in varargs
        case 'D': v.d = src.f64(); break;
        case 'Z': v.i = int32_t(src.u32() & 0xff); break;
        case 'B': v.i = int8_t(src.u32()); break;
        case 'C': v.i = int32_t(src.u32() & 0xffff); break;
        case 'S': v.i = int16_t(src.u32()); break;
        case 'I': v.i = src.i32(); break;
        default: v.obj = src.u32();
      }
    }
    return out;
  }

  void setReturn(char ret, const JValue& v) {
    switch (ret) {
      case 'V': break;
      case 'J': g.ret64(uint64_t(v.j)); break;
      case 'F': g.retF(v.f); break;
      case 'D': g.retD(v.d); break;
      case 'L': g.ret32(v.obj); break;
      case 'Z': g.ret32(uint32_t(v.i) & 0xff); break;
      default: g.ret32(uint32_t(v.i));
    }
  }

  void dispatch(const std::string& name) {
    JavaHost& j = e.java();
    Args a(g);
    a.u32();  // JNIEnv*
    if (name == "GetVersion") return g.ret32(0x00010006);
    if (name == "FindClass") return g.ret32(j.findClass(g.str(a.u32())));
    if (name == "GetStaticMethodID" || name == "GetMethodID") {
      uint32_t cls = a.u32();
      std::string m = g.str(a.u32()), sig = g.str(a.u32());
      return g.ret32(j.methodId(cls, m, sig, name == "GetStaticMethodID"));
    }
    if (name.rfind("Call", 0) == 0 && name.find("Nonvirtual") == std::string::npos) {
      bool is_static = name.rfind("CallStatic", 0) == 0;
      uint32_t target = a.u32(), method = a.u32();
      char mode = name.back() == 'V' ? 'V' : name.back() == 'A' ? 'A' : '.';
      char ret;
      std::vector<JValue> args = readArgs(method, a, mode, ret);
      return setReturn(ret, j.call(method, target, is_static, args));
    }
    if (name == "NewStringUTF") return g.ret32(j.newStringUtf(g.str(a.u32())));
    if (name == "GetStringUTFChars") {
      uint32_t s = a.u32(), is_copy = a.u32();
      if (is_copy) g.w8(is_copy, 1);
      return g.ret32(g.allocCString(j.stringUtf(s)));
    }
    if (name == "ReleaseStringUTFChars" || name == "ReleaseStringChars") {
      a.u32();
      g.free(a.u32());
      return;
    }
    if (name == "GetStringUTFLength") return g.ret32(uint32_t(j.stringUtf(a.u32()).size()));
    if (name == "GetStringLength") return g.ret32(uint32_t(j.stringUtf16(a.u32()).size()));
    if (name == "GetStringChars") {
      uint32_t s = a.u32(), is_copy = a.u32();
      std::u16string u = j.stringUtf16(s);
      u.push_back(0);
      if (is_copy) g.w8(is_copy, 1);
      return g.ret32(g.allocBytes(u.data(), uint32_t(u.size() * 2)));
    }
    if (name == "GetStringRegion" || name == "GetStringUTFRegion") {
      uint32_t s = a.u32();
      int32_t start = a.i32(), len = a.i32();
      uint32_t buf = a.u32();
      std::u16string u = j.stringUtf16(s).substr(size_t(start), size_t(len));
      if (name == "GetStringRegion") {
        std::memcpy(g.host(buf), u.data(), u.size() * 2);
      } else {
        std::string m = utf16ToModifiedUtf8(u);
        std::memcpy(g.host(buf), m.c_str(), m.size() + 1);
      }
      return;
    }
    if (name == "DeleteLocalRef") return j.deleteLocalRef(a.u32());
    if (name == "NewGlobalRef" || name == "NewWeakGlobalRef") return g.ret32(j.newGlobalRef(a.u32()));
    if (name == "DeleteGlobalRef" || name == "DeleteWeakGlobalRef") return j.deleteGlobalRef(a.u32());
    if (name == "NewLocalRef") return g.ret32(a.u32());
    if (name == "GetObjectClass") return g.ret32(j.objectClass(a.u32()));
    if (name == "IsSameObject") {
      uint32_t x = a.u32(), y = a.u32();
      return g.ret32(j.isSameObject(x, y));
    }
    if (name == "ExceptionCheck") return g.ret32(j.exceptionCheck());
    if (name == "ExceptionOccurred") return g.ret32(j.exceptionCheck() ? 1 : 0);
    if (name == "ExceptionClear") return j.exceptionClear();
    if (name == "ExceptionDescribe") return j.exceptionDescribe();
    if (name == "GetJavaVM") {
      g.w32(a.u32(), vm);
      return g.ret32(0);
    }
    if (name == "PushLocalFrame" || name == "EnsureLocalCapacity") return g.ret32(0);
    if (name == "PopLocalFrame") return g.ret32(a.u32());
    if (name == "GetArrayLength") return g.ret32(uint32_t(j.arrayLength(a.u32())));
    g.fatal("engine called unsupported JNI function " + name + " from " + g.symbolize(g.reg(kLR)));
  }

  // JavaVM invoke interface: DestroyJavaVM(3), Attach(4), Detach(5), GetEnv(6), AttachDaemon(7)
  void onVm(uint32_t index) {
    if (index == 4 || index == 6 || index == 7) {
      uint32_t penv = g.reg(kR1);
      if (penv) g.w32(penv, env);
    }
    g.ret32(0);
  }
};

}  // namespace

void installJni(Engine& e) {
  auto* jni = new Jni(e);
  Guest& g = e.guest();
  g.w32(jni->env, jni->table);
  for (size_t i = 0; i < jni->names.size(); i++) g.w32(jni->table + 4 * uint32_t(i), jni->stubs + 8 * uint32_t(i));
  g.addTrapTable(jni->stubs, uint32_t(jni->names.size()),
                 [jni](Guest&, uint32_t index) { jni->dispatch(jni->names[index]); });
  g.w32(jni->vm, jni->vmTable);
  for (uint32_t i = 0; i < 8; i++) g.w32(jni->vmTable + 4 * i, jni->vmStubs + 8 * i);
  g.addTrapTable(jni->vmStubs, 8, [jni](Guest&, uint32_t index) { jni->onVm(index); });
  e.guestEnv_ = jni->env;
  e.guestVm_ = jni->vm;

  // --- NDK asset manager: AAsset = guest {buffer, length} -------------------------
  auto def = [&](const char* name, Thunk t) { g.setThunk(name, std::move(t)); };
  auto* managers = new std::vector<uint32_t>();  // AAssetManager* token -> Java object (global ref)
  e.onShutdown([jni, managers] {
    delete managers;  // their global refs go with the JavaHost
    delete jni;
  });
  def("AAssetManager_fromJava", [&e, managers](Guest& g, Args& a) {
    a.u32();
    uint32_t global = e.java().newGlobalRef(a.u32());
    managers->push_back(global);
    g.ret32(uint32_t(managers->size()));  // token 1..n
  });
  def("AAssetManager_open", [&e, managers](Guest& g, Args& a) {
    uint32_t token = a.u32();
    std::string name = g.str(a.u32());
    if (token == 0 || token > managers->size()) return g.ret32(0);
    std::vector<uint8_t> bytes;
    if (!e.java().readAsset((*managers)[token - 1], name, bytes)) return g.ret32(0);
    uint32_t buffer = g.malloc(uint32_t(bytes.size()) + 1);
    if (!bytes.empty()) std::memcpy(g.host(buffer), bytes.data(), bytes.size());
    g.w8(buffer + uint32_t(bytes.size()), 0);
    uint32_t asset = g.malloc(8);
    g.w32(asset, buffer);
    g.w32(asset + 4, uint32_t(bytes.size()));
    g.ret32(asset);
  });
  def("AAsset_getLength", [](Guest& g, Args& a) { g.ret32(g.r32(a.u32() + 4)); });
  def("AAsset_getBuffer", [](Guest& g, Args& a) { g.ret32(g.r32(a.u32())); });
  def("AAsset_close", [](Guest& g, Args& a) {
    uint32_t asset = a.u32();
    g.free(g.r32(asset));
    g.free(asset);
  });
}

}  // namespace nin

// Real Java side for the emulated engine: guest JNI calls -> the app's JNIEnv.
#pragma once

#include <jni.h>

#include <map>
#include <string>
#include <vector>

#include "engine.h"

namespace nin {

class AndroidJavaHost : public JavaHost {
 public:
  // Called at every entry from Java (the JNIEnv is per thread).
  void setEnv(JNIEnv* env) { env_ = env; }
  JNIEnv* env() const { return env_; }

  // Guest handle for a host reference owned by the current native frame.
  uint32_t wrapLocal(jobject obj);
  jobject unwrap(uint32_t handle) const;

  uint32_t findClass(const std::string& name) override;
  uint32_t methodId(uint32_t cls, const std::string& name, const std::string& sig, bool is_static) override;
  const std::string& methodSignature(uint32_t method) override;
  JValue call(uint32_t method, uint32_t target, bool is_static, const std::vector<JValue>& args) override;
  uint32_t newStringUtf(const std::string& modified_utf8) override;
  std::string stringUtf(uint32_t str) override;
  std::u16string stringUtf16(uint32_t str) override;
  uint32_t objectClass(uint32_t obj) override;
  void deleteLocalRef(uint32_t obj) override;
  uint32_t newGlobalRef(uint32_t obj) override;
  void deleteGlobalRef(uint32_t obj) override;
  bool isSameObject(uint32_t a, uint32_t b) override;
  bool exceptionCheck() override;
  void exceptionClear() override;
  void exceptionDescribe() override;
  int32_t arrayLength(uint32_t array) override;
  bool readAsset(uint32_t asset_manager, const std::string& name, std::vector<uint8_t>& out) override;
  void endCall() override;
  // Deletes every global reference the engine made, before the host is
  // destroyed with it (see NINLib.restartEngine). Needs this thread's env.
  void releaseGlobals();

 private:
  struct Slot {
    jobject ref = nullptr;
    bool global = false;
    bool used = false;
  };
  struct Method {
    jmethodID id;
    std::string sig;
    char ret;
  };
  uint32_t store(jobject ref, bool global);
  void clearPendingException(const char* where);

  JNIEnv* env_ = nullptr;
  std::vector<Slot> slots_;
  std::vector<uint32_t> freeSlots_;
  std::vector<uint32_t> locals_;
  std::map<std::string, uint32_t> classes_;  // class name -> global handle
  std::vector<Method> methods_;
  std::map<jmethodID, uint32_t> methodIndex_;
};

}  // namespace nin

// Host-side runtime around the emulated original engine.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "guest.h"

namespace nin {

// Value passed across the (guest) JNI boundary. Objects are guest handles.
struct JValue {
  char type = 'V';  // JNI signature letter; 'L' for any object
  union {
    int32_t i;
    int64_t j;
    float f;
    double d;
    uint32_t obj;
  };
  JValue() : j(0) {}
};

// The Java side as seen by the engine. Android: real JNI (jni_bridge.cpp).
class JavaHost {
 public:
  virtual ~JavaHost() = default;
  virtual uint32_t findClass(const std::string& name) = 0;
  virtual uint32_t methodId(uint32_t cls, const std::string& name, const std::string& sig,
                            bool is_static) = 0;
  virtual const std::string& methodSignature(uint32_t method) = 0;
  virtual JValue call(uint32_t method, uint32_t target, bool is_static, const std::vector<JValue>& args) = 0;
  virtual uint32_t newStringUtf(const std::string& modified_utf8) = 0;
  virtual std::string stringUtf(uint32_t str) = 0;       // modified UTF-8
  virtual std::u16string stringUtf16(uint32_t str) = 0;
  virtual uint32_t objectClass(uint32_t obj) = 0;
  virtual void deleteLocalRef(uint32_t obj) = 0;
  virtual uint32_t newGlobalRef(uint32_t obj) = 0;
  virtual void deleteGlobalRef(uint32_t obj) = 0;
  virtual bool isSameObject(uint32_t a, uint32_t b) = 0;
  virtual bool exceptionCheck() = 0;
  virtual void exceptionClear() = 0;
  virtual void exceptionDescribe() = 0;
  virtual int32_t arrayLength(uint32_t array) = 0;
  // Asset access for AAssetManager_* (the Java AssetManager object -> bytes).
  virtual bool readAsset(uint32_t asset_manager, const std::string& name, std::vector<uint8_t>& out) = 0;
  // Called when a top-level native call returns (release local references).
  virtual void endCall() = 0;
};

struct Platform {
  // Map a guest path to a host path (identity on device, sandbox on desktop).
  std::string sandbox_root;
  // Deterministic virtual clock for desktop tests; real clock when unset.
  bool virtual_clock = false;
  double virtual_now = 1577836800.0;
  bool echo_engine_logs = true;
};

class Engine {
 public:
  Engine(JavaHost* java, Platform platform);
  ~Engine();
  // Load and link the original library, then run its static constructors.
  void boot(const std::vector<uint8_t>& image);
  // Call an exported JNI function of the original library by name.
  uint64_t callExport(const std::string& name, const std::vector<uint32_t>& words);
  // Releases what the bridges hold outside guest memory (host files, GL
  // objects), before the engine is destroyed to boot a fresh one. Runs on the
  // GL thread. Not run at process exit.
  void shutdown();
  void onShutdown(std::function<void()> release) { shutdown_.push_back(std::move(release)); }
  Guest& guest() { return guest_; }
  JavaHost& java() { return *java_; }
  Platform& platform() { return platform_; }
  uint32_t guestEnv() const { return guestEnv_; }
  std::recursive_mutex& lock() { return lock_; }

  // Filled by the bridges.
  uint32_t guestEnv_ = 0;
  uint32_t guestVm_ = 0;

 private:
  Guest guest_;
  JavaHost* java_;
  Platform platform_;
  std::recursive_mutex lock_;
  std::vector<std::function<void()>> shutdown_;
};

void installBionic(Engine& e);
void setFileTracing(bool on);  // debug: log paths the engine opens (NIN_TRACE_FILES)
void installGles(Engine& e);
void installJni(Engine& e);

}  // namespace nin

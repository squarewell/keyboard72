// The 15 com.jormy.nin.NINLib natives, implemented by running the original
// 32-bit engine (asset nintype/libgl2jni-armeabi.so) under emulation.
//
// Boot is lazy and happens on the first call that needs the engine; syncTiming
// only records its argument (it arrives on the main thread before the GL
// thread exists) and is replayed with the current time right before boot's
// first init, which keeps the Java-millis/engine-clock anchor consistent.
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>
#include <sys/system_properties.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>

#include "android_java.h"
#include "backup.h"
#include "boot_log.h"
#include "cpu.h"
#include "engine.h"
#include "log.h"

namespace {

constexpr const char* kEngineAsset = "nintype/libgl2jni-armeabi.so";

std::mutex g_bootLock;
std::unique_ptr<nin::AndroidJavaHost> g_java;
std::unique_ptr<nin::Engine> g_engine;

int64_t nowMillis() {
  timeval tv{};
  gettimeofday(&tv, nullptr);
  return int64_t(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

std::vector<uint8_t> readEngineImage(JNIEnv* env) {
  jclass utils = env->FindClass("com/jormy/nin/Utils");
  jmethodID get = env->GetStaticMethodID(utils, "assetManager", "()Landroid/content/res/AssetManager;");
  jobject jmgr = env->CallStaticObjectMethod(utils, get);
  AAssetManager* mgr = AAssetManager_fromJava(env, jmgr);
  AAsset* asset = mgr ? AAssetManager_open(mgr, kEngineAsset, AASSET_MODE_BUFFER) : nullptr;
  if (!asset) {
    NIN_LOGE("engine asset %s missing from the APK", kEngineAsset);
    std::abort();
  }
  auto* data = static_cast<const uint8_t*>(AAsset_getBuffer(asset));
  std::vector<uint8_t> image(data, data + AAsset_getLength(asset));
  AAsset_close(asset);
  env->DeleteLocalRef(jmgr);
  env->DeleteLocalRef(utils);
  return image;
}

// Start-up progress for the keyboard's boot screen (NINLib.engineStage), read
// from the UI thread while the GL thread boots and initializes the engine.
enum Stage : int { kNoEngine = 0, kLoading = 1, kStarting = 2, kBooted = 3, kInitializing = 4, kDrawing = 5, kReady = 6 };
std::atomic<int> g_stage{kNoEngine};
int g_stepsDrawn = 0;  // GL thread

// Holds the engine lock for one JNI entry and releases local handles after.
class Entry {
 public:
  Entry(JNIEnv* env, jclass cls) {
    {
      std::lock_guard<std::mutex> boot(g_bootLock);
      if (!g_engine) {
        g_java = std::make_unique<nin::AndroidJavaHost>();
        nin::Platform platform;
        // The engine logs several lines per frame; forwarding them to logcat costs
        // a syscall each. Opt in with `adb shell setprop debug.nin.enginelogs 1`.
        char prop[PROP_VALUE_MAX] = {};
        __system_property_get("debug.nin.enginelogs", prop);
        platform.echo_engine_logs = prop[0] == '1';
        platform.before_constructors = [] {
          g_stage = kStarting;
          nin::bootLog(nin::BootLine::Step, "starting engine");
        };
        nin::bootLogBegin();
        nin::bootLog(nin::BootLine::Step, "loading engine");
        g_stage = kLoading;
        g_engine = std::make_unique<nin::Engine>(g_java.get(), platform);
        g_java->setEnv(env);
        std::lock_guard<std::recursive_mutex> engine(g_engine->lock());
        int64_t t0 = nowMillis();
        std::vector<uint8_t> image = readEngineImage(env);
        NIN_LOGI("engine image read in %lld ms", (long long)(nowMillis() - t0));
        char size[48];
        std::snprintf(size, sizeof size, "read libgl2jni.so, %.1f MB (armeabi)", image.size() / 1048576.0);
        nin::bootLog(nin::BootLine::Info, size);
        g_engine->boot(image);
        g_java->endCall();
        g_stage = kBooted;
        NIN_LOGI("engine booted in %lld ms", (long long)(nowMillis() - t0));
      }
    }
    lock_ = std::unique_lock<std::recursive_mutex>(g_engine->lock());
    ++nesting();
    g_java->setEnv(env);
    cls_ = g_java->wrapLocal(cls);
  }
  ~Entry() {
    if (--nesting() == 0) g_java->endCall();
  }
  uint32_t cls() const { return cls_; }
  uint32_t str(jstring s) { return g_java->wrapLocal(s); }
  uint64_t call(const char* name, std::vector<uint32_t> words) {
    words.insert(words.begin(), {g_engine->guestEnv(), cls_});
    return g_engine->callExport(std::string("Java_com_jormy_nin_NINLib_") + name, words);
  }

 private:
  static int& nesting() {
    static thread_local int n = 0;
    return n;
  }
  std::unique_lock<std::recursive_mutex> lock_;
  uint32_t cls_ = 0;
};

uint32_t bits(float f) {
  uint32_t w;
  std::memcpy(&w, &f, 4);
  return w;
}

bool g_synced = false;


double monotonicMs() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

// Lightweight performance log: every 2 s, frames per second and average/max
// time spent in the engine per frame (step) and per touch event, as seen from Java.
struct Stats {
  double windowStart = 0;
  int frames = 0, touches = 0;
  double frameTotal = 0, frameMax = 0, touchTotal = 0, touchMax = 0;
  void frame(double now, double ms) {
    if (windowStart == 0) windowStart = now;
    frames++;
    frameTotal += ms;
    frameMax = std::max(frameMax, ms);
    if (now - windowStart >= 2000) {
      NIN_LOGI("perf: %.0f frames/s, step avg %.2f ms max %.2f ms | touch avg %.2f ms max %.2f ms (%d touches)",
               frames * 1000.0 / (now - windowStart), frameTotal / frames, frameMax,
               touches ? touchTotal / touches : 0.0, touchMax, touches);
      *this = Stats();
      windowStart = now;
    }
  }
  void touch(double ms) {
    touches++;
    touchTotal += ms;
    touchMax = std::max(touchMax, ms);
  }
} g_stats;

// The Java render loop relies on eglSwapBuffers to wait for the display. With
// the screen off (the original app paused rendering on lock; this fork keeps
// it running so the keyboard works on the lock screen), or while the engine is
// idle and draws nothing, the swap returns at once and the loop spins at
// thousands of steps per second. Keep steps at least 4 ms apart: shorter than
// any display's refresh interval, so normal frames never wait.
void paceSteps() {
  constexpr double kMinStepMs = 4.0;
  static double last = 0;  // only the GL thread steps
  double now = monotonicMs();
  if (now - last < kMinStepMs) {
    usleep(useconds_t((kMinStepMs - (now - last)) * 1000));
    now = monotonicMs();
  }
  last = now;
}


void maybeSync(Entry& e) {
  if (g_synced) return;
  g_synced = true;
  // The original synced when the view was created; use the current time so the
  // Java-millis/engine-clock anchor stays consistent after the (slow) boot.
  uint64_t ms = uint64_t(nowMillis());
  e.call("syncTiming", {uint32_t(ms), uint32_t(ms >> 32)});
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_com_jormy_nin_NINLib_syncTiming(JNIEnv*, jclass, jlong) {
  // Called from the NINView constructor on the main thread: deferred to the
  // first engine call on the GL thread (see maybeSync).
  return 0;
}

JNIEXPORT void JNICALL Java_com_jormy_nin_NINLib_init(JNIEnv* env, jclass cls, jint w, jint h, jint sw, jint sh) {
  Entry e(env, cls);
  maybeSync(e);
  int64_t t0 = nowMillis();
  if (g_stage < kInitializing) {
    g_stage = kInitializing;
    nin::bootLog(nin::BootLine::Step, "loading dictionaries + layout");
  }
  e.call("init", {uint32_t(w), uint32_t(h), uint32_t(sw), uint32_t(sh)});
  if (g_stage == kInitializing) {
    g_stage = kDrawing;
    g_stepsDrawn = 0;
    nin::bootLog(nin::BootLine::Step, "drawing keyboard");
  }
  NIN_LOGI("engine init (%dx%d) in %lld ms", w, h, (long long)(nowMillis() - t0));
}

JNIEXPORT void JNICALL Java_com_jormy_nin_NINLib_step(JNIEnv* env, jclass cls) {
  paceSteps();  // before taking the engine lock
  Entry e(env, cls);
  maybeSync(e);
  double t0 = monotonicMs();
  e.call("step", {});
  double t1 = monotonicMs();
  // The first frames after init compile the drawing code (~0.2 s each); the
  // second one shows the whole keyboard.
  if (g_stage == kDrawing && ++g_stepsDrawn >= 2) {
    g_stage = kReady;
    nin::bootLogEnd();
  }
  g_stats.frame(t1, t1 - t0);
}

// The app's "fast memory access" setting (not part of the original API). Called
// before the engine boots; it takes effect at the next boot. No Entry: this
// must not boot the engine.
JNIEXPORT void JNICALL Java_com_jormy_nin_NINLib_setJitFastmem(JNIEnv*, jclass, jboolean on) {
  nin::setAppJitFastmem(on);
}

// Restart from the launch page (not part of the original API): shuts the
// running engine down so the next call boots a fresh one, which reads settings
// such as fast memory access again. Called on the GL thread, outside any other
// engine call, with the EGL context current (the engine's GL objects are
// released). Every engine call comes from this thread, so none is waiting on
// the engine's lock; the boot lock keeps any other from booting meanwhile.
// Returns whether an engine was running. Its saved state (words, settings) is
// on disk; what it held only in memory is gone, as after a process restart.
JNIEXPORT jboolean JNICALL Java_com_jormy_nin_NINLib_restartEngine(JNIEnv* env, jclass) {
  std::lock_guard<std::mutex> boot(g_bootLock);
  if (!g_engine) return JNI_FALSE;
  {
    std::lock_guard<std::recursive_mutex> engine(g_engine->lock());
    g_java->setEnv(env);
    g_engine->shutdown();
    g_java->releaseGlobals();
  }
  g_engine.reset();
  g_java.reset();
  g_synced = false;
  g_stage = kNoEngine;
  NIN_LOGI("engine shut down for a restart");
  return JNI_TRUE;
}

// The boot screen's log lines from `next` on (boot_log.h), as UTF-8 bytes:
// engine lines are not guaranteed to be valid modified UTF-8 for NewStringUTF.
JNIEXPORT jbyteArray JNICALL Java_com_jormy_nin_NINLib_bootLog(JNIEnv* env, jclass, jint next) {
  std::string text = nin::bootLogRead(uint32_t(next));
  jbyteArray out = env->NewByteArray(jsize(text.size()));
  env->SetByteArrayRegion(out, 0, jsize(text.size()), reinterpret_cast<const jbyte*>(text.data()));
  return out;
}

// Start-up progress (Stage above). No locks: polled from the UI thread.
JNIEXPORT jint JNICALL Java_com_jormy_nin_NINLib_engineStage(JNIEnv*, jclass) { return g_stage.load(); }

// Whether the running engine's CPU uses fast memory access (false before boot).
JNIEXPORT jboolean JNICALL Java_com_jormy_nin_NINLib_jitFastmemActive(JNIEnv*, jclass) {
  std::lock_guard<std::mutex> boot(g_bootLock);
  return g_engine && nin::jitFastmemActive() ? JNI_TRUE : JNI_FALSE;
}

// The field's keyboard and return key types (not part of the original API):
// the globals the engine's iOS host set, watched by Moopad::update.
JNIEXPORT void JNICALL Java_com_jormy_nin_NINLib_setFieldTypes(JNIEnv* env, jclass cls, jint keyboardType,
                                                              jint returnKeyType) {
  Entry e(env, cls);
  g_engine->callExport("_Z27ninkeyboard_on_keyboardtypei", {uint32_t(keyboardType)});
  g_engine->callExport("_Z36ninkeyboard_on_keyboardreturnkeytypei", {uint32_t(returnKeyType)});
}

// Backups for the launch page (not part of the original API). Called on the GL
// thread while the keyboard is shown; see backup.h.
JNIEXPORT jbyteArray JNICALL Java_com_jormy_nin_NINLib_backupExport(JNIEnv* env, jclass cls, jint kind) {
  Entry e(env, cls);
  std::string text;
  if (!nin::exportBackup(*g_engine, kind, text)) return nullptr;
  jbyteArray out = env->NewByteArray(jsize(text.size()));
  env->SetByteArrayRegion(out, 0, jsize(text.size()), reinterpret_cast<const jbyte*>(text.data()));
  return out;
}

JNIEXPORT jint JNICALL Java_com_jormy_nin_NINLib_backupImport(JNIEnv* env, jclass cls, jbyteArray text) {
  Entry e(env, cls);
  std::string bytes(size_t(env->GetArrayLength(text)), '\0');
  env->GetByteArrayRegion(text, 0, jsize(bytes.size()), reinterpret_cast<jbyte*>(bytes.data()));
  return nin::importBackup(*g_engine, bytes);
}

JNIEXPORT void JNICALL Java_com_jormy_nin_NINLib_memTestStep(JNIEnv* env, jclass cls) {
  Entry e(env, cls);
  e.call("memTestStep", {});
}

JNIEXPORT void JNICALL Java_com_jormy_nin_NINLib_onTouchEvent(JNIEnv* env, jclass cls, jint id, jint action, jfloat x,
                                                             jfloat y, jfloat pressure, jfloat size, jlong ms) {
  Entry e(env, cls);
  maybeSync(e);
  double t0 = monotonicMs();
  e.call("onTouchEvent", {uint32_t(id), uint32_t(action), bits(x), bits(y), bits(pressure), bits(size),
                          uint32_t(uint64_t(ms)), uint32_t(uint64_t(ms) >> 32)});
  g_stats.touch(monotonicMs() - t0);
}

JNIEXPORT void JNICALL Java_com_jormy_nin_NINLib_onChangeAppOrTextbox(JNIEnv* env, jclass cls, jstring a, jstring b,
                                                                     jstring c) {
  Entry e(env, cls);
  maybeSync(e);
  e.call("onChangeAppOrTextbox", {e.str(a), e.str(b), e.str(c)});
}

JNIEXPORT void JNICALL Java_com_jormy_nin_NINLib_onEditorChangeTypeClass(JNIEnv* env, jclass cls, jstring a,
                                                                        jstring b) {
  Entry e(env, cls);
  maybeSync(e);
  e.call("onEditorChangeTypeClass", {e.str(a), e.str(b)});
}

JNIEXPORT void JNICALL Java_com_jormy_nin_NINLib_onExternalSelChange(JNIEnv* env, jclass cls) {
  Entry e(env, cls);
  maybeSync(e);
  e.call("onExternalSelChange", {});
}

JNIEXPORT void JNICALL Java_com_jormy_nin_NINLib_onTextSelection(JNIEnv* env, jclass cls, jstring a, jstring b,
                                                                jstring c, jstring d) {
  Entry e(env, cls);
  maybeSync(e);
  e.call("onTextSelection", {e.str(a), e.str(b), e.str(c), e.str(d)});
}

JNIEXPORT void JNICALL Java_com_jormy_nin_NINLib_onWordDestruction(JNIEnv* env, jclass cls, jstring a, jstring b) {
  Entry e(env, cls);
  maybeSync(e);
  e.call("onWordDestruction", {e.str(a), e.str(b)});
}

JNIEXPORT jint JNICALL Java_com_jormy_nin_NINLib_processBackspaceAllowance(JNIEnv* env, jclass cls, jstring a,
                                                                         jstring b, jint n) {
  Entry e(env, cls);
  maybeSync(e);
  return jint(uint32_t(e.call("processBackspaceAllowance", {e.str(a), e.str(b), uint32_t(n)})));
}

JNIEXPORT jint JNICALL Java_com_jormy_nin_NINLib_getUnicodeBackIndex(JNIEnv* env, jclass cls, jstring s, jint i) {
  Entry e(env, cls);
  return jint(uint32_t(e.call("getUnicodeBackIndex", {e.str(s), uint32_t(i)})));
}

JNIEXPORT jint JNICALL Java_com_jormy_nin_NINLib_getUnicodeFrontIndex(JNIEnv* env, jclass cls, jstring s, jint i) {
  Entry e(env, cls);
  return jint(uint32_t(e.call("getUnicodeFrontIndex", {e.str(s), uint32_t(i)})));
}

JNIEXPORT jlong JNICALL Java_com_jormy_nin_NINLib_processSoftKeyboardCursorMovementLeft(JNIEnv* env, jclass cls,
                                                                                      jstring s) {
  Entry e(env, cls);
  maybeSync(e);
  return jlong(e.call("processSoftKeyboardCursorMovementLeft", {e.str(s)}));
}

JNIEXPORT jlong JNICALL Java_com_jormy_nin_NINLib_processSoftKeyboardCursorMovementRight(JNIEnv* env, jclass cls,
                                                                                       jstring s) {
  Entry e(env, cls);
  maybeSync(e);
  return jlong(e.call("processSoftKeyboardCursorMovementRight", {e.str(s)}));
}

}  // extern "C"

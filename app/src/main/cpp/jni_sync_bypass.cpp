#include "jni_sync_bypass.h"

#include <cstdlib>
#include <cstring>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

#include "engine_patch.h"
#include "log.h"

namespace nin {

bool jniSyncBypassRequested() {
  if (const char* value = std::getenv("NIN_JNI_SYNC_BYPASS")) return std::strcmp(value, "1") == 0;
#ifdef __ANDROID__
  char value[PROP_VALUE_MAX] = {};
  __system_property_get("debug.nin.jni_sync_bypass", value);
  return std::strcmp(value, "0") != 0;  // on in the app unless set to 0
#else
  return false;
#endif
}

bool installJniSyncBypass(Guest& g, const std::vector<uint8_t>& image) {
  // JNI::initenv: branch over its inline clear(classes)/clear(shortcuts).
  // env, thread, lastthreadpoint and global_clazz_object_version still update.
  // Argument-bearing calls still run Class::maybeRefreshClazz. AndroidJavaHost
  // owns global class refs and stable method IDs for this engine's lifetime.
  // The explicit resetClassesAndMethods entry remains fully functional.
  constexpr EnginePatch patches[] = {{0x0121f9cc, 0xe59f01e8, 0xea00003b}};
  if (!installEnginePatches(g, image, patches, 1, "JNI sync bypass")) return false;
  NIN_LOGI("JNI sync bypass enabled: retain guest class/method caches (1 verified ARM patch)");
  return true;
}

}  // namespace nin

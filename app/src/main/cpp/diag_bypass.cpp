#include "diag_bypass.h"

#include <cstdlib>
#include <cstring>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

#include "engine_patch.h"
#include "log.h"

namespace nin {
namespace {

// ELF-relative ARM instructions. See DIAGNOSTICS.md for side effects/ABI.
constexpr EnginePatch patches[] = {
    {0x005b1418, 0xe92d4bf0, 0xe12fff1e},  // Moopad::trackMemoryUsage: void return
    {0x006aaa4c, 0xe92d4830, 0xe12fff1e},  // its wrapper: omit temporary tag too
    {0x00edc040, 0xe1a08002, 0xe3a08000},  // QB ctor: enabled argument = false
    {0x00edc17c, 0xe1a05002, 0xe3a05000},  // QB::init (QBMutex ctor): same gate
};

}  // namespace

bool diagnosticBypassRequested() {
  if (const char* value = std::getenv("NIN_DIAG_BYPASS")) return std::strcmp(value, "1") == 0;
#ifdef __ANDROID__
  char value[PROP_VALUE_MAX] = {};
  __system_property_get("debug.nin.diag_bypass", value);
  return std::strcmp(value, "0") != 0;  // on in the app unless set to 0
#else
  return false;
#endif
}

bool installDiagnosticBypass(Guest& g, const std::vector<uint8_t>& image) {
  if (!installEnginePatches(g, image, patches, 4, "diagnostic bypass")) return false;
  NIN_LOGI("diagnostic bypass enabled: Moopad tracking and QB reporting (4 verified ARM patches)");
  return true;
}

}  // namespace nin

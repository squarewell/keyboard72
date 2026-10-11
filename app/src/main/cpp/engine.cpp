#include "engine.h"

#include <algorithm>
#include <cstring>

#include "log.h"
#include "diag_bypass.h"
#include "jni_sync_bypass.h"
#include "memory_query.h"
#include "backup.h"
#include "hot_dispatch.h"

namespace nin {

Engine::Engine(JavaHost* java, Platform platform) : java_(java), platform_(std::move(platform)) {}
Engine::~Engine() = default;

void Engine::shutdown() {
  for (auto it = shutdown_.rbegin(); it != shutdown_.rend(); ++it) (*it)();
  shutdown_.clear();
}

namespace {

// The engine labels itself with its build date ("v. Sep  9 2017" in the
// spacebar menu, "NIN v. ..." on restart, the options page). Show the app's
// version instead: the date inside those read-only strings is overwritten in
// guest memory (same length or shorter). The feedback template's
// "Compilation date" stays, since that is still true.
void patchVersionLabels(Guest& g, const std::string& label) {
  const std::string date = "Sep  9 2017";
  if (label.size() > date.size()) {
    NIN_LOGW("version label '%s' longer than %zu characters; not shown", label.c_str(), date.size());
    return;
  }
  uint8_t* image = g.host(kImageBase);
  const size_t size = g.imageEnd() - kImageBase;
  for (const char* text : {"v. Sep  9 2017", "NIN v. Sep  9 2017 \nprocess restarted", "simplified options v.Sep  9 2017"}) {
    const std::string needle(text, std::strlen(text) + 1);  // whole string, with its terminator
    uint8_t* hit = std::search(image, image + size, needle.begin(), needle.end());
    if (hit == image + size) continue;
    std::string replaced = text;
    replaced.replace(replaced.find(date), date.size(), label);
    std::memset(hit, 0, needle.size());
    std::memcpy(hit, replaced.data(), replaced.size());
  }
}

}  // namespace

void Engine::boot(const std::vector<uint8_t>& image) {
  guest_.loadElf(image);
  if (diagnosticBypassRequested()) installDiagnosticBypass(guest_, image);
  if (jniSyncBypassRequested()) installJniSyncBypass(guest_, image);
  if (floatDispatchRequested()) installFloatDispatch(guest_, image);
  if (byteCasRequested()) installByteCas(guest_, image);
  if (refcountAddRequested()) installRefcountAdd(guest_, image);
  installMemoryQuery(guest_, image);
  installBackupPrompts(guest_, image);
#ifdef NIN_VERSION_LABEL
  patchVersionLabels(guest_, NIN_VERSION_LABEL);
#endif
  guest_.installNativeBlob();
  installBionic(*this);
  installGles(*this);
  installJni(*this);
  guest_.runConstructors();
  NIN_LOGI("engine constructors done");
}

uint64_t Engine::callExport(const std::string& name, const std::vector<uint32_t>& words) {
  uint32_t address = guest_.symbol(name);
  if (!address) guest_.fatal("engine does not export " + name);
  const uint64_t result = guest_.call(address, words);
  return result;
}

}  // namespace nin

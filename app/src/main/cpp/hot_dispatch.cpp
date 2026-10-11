#include "hot_dispatch.h"

#include <cstdlib>
#include <cstring>
#include <iterator>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

#include "engine_patch.h"
#include "log.h"

namespace nin {
namespace {
#include "float_dispatch_sites.inc"
#include "byte_cas_sites.inc"
#include "refcount_add_sites.inc"

bool requested(const char* env, const char* property) {
  if (const char* value = std::getenv(env)) return std::strcmp(value, "1") == 0;
#ifdef __ANDROID__
  char value[PROP_VALUE_MAX] = {};
  __system_property_get(property, value);
  return std::strcmp(value, "0") != 0;  // on in the app unless set to 0
#else
  return false;
#endif
}
}  // namespace

bool floatDispatchRequested() {
  return requested("NIN_FLOAT_DISPATCH", "debug.nin.float_dispatch");
}
bool byteCasRequested() {
  return requested("NIN_BYTE_CAS", "debug.nin.byte_cas");
}

bool refcountAddRequested() {
  return requested("NIN_REFCOUNT_ADD", "debug.nin.refcount_add");
}

bool installFloatDispatch(Guest& g, const std::vector<uint8_t>& image) {
  // Replace 39 ARM float PLT veneers with ldr ip,[pc]; b original_entry;
  // literal_GOT_address. Keep the original r12, LR, flags and argument words.
  // Original entries still select the normal VFP/legacy arithmetic path.
  if (!installEnginePatches(g, image, kFloatDispatchPatches, std::size(kFloatDispatchPatches),
                            "float dispatch")) return false;
  NIN_LOGI("float dispatch enabled: 39 verified veneers and GOT targets");
  return true;
}

bool installByteCas(Guest& g, const std::vector<uint8_t>& image) {
  // The shim runs one guest thread. This internal byte compare-exchange keeps
  // all registers, NZCV, stack writes and the successful aligned-word store,
  // while using a byte load for the common failed comparison.
  if (!installEnginePatches(g, image, kByteCasPatches, std::size(kByteCasPatches),
                            "byte compare-exchange")) return false;
  NIN_LOGI("byte compare-exchange enabled: verified single-threaded leaf");
  return true;
}
bool installRefcountAdd(Guest& g, const std::vector<uint8_t>& image) {
  // Both add helpers retain their original stack footprint, scratch-register
  // results, flags and modulo-2^32 update. The existing single-guest-thread
  // contract makes their compare-exchange retries/barrier calls redundant.
  if (!installEnginePatches(g, image, kRefcountAddPatches, std::size(kRefcountAddPatches),
                            "refcount add")) return false;
  NIN_LOGI("refcount add enabled: 2 verified single-threaded leaves");
  return true;
}
}  // namespace nin

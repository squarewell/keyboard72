#include "field_types.h"

#include "engine_patch.h"
#include "guest.h"
#include "log.h"

namespace nin {
namespace {

// Moopad::updateAndMonitorIOSKeyboardTypeChange turns on URL peck mode when the
// return key type is go, Google, search or Yahoo (iOS UIReturnKeyType 1, 2, 6,
// 8: mask 0xa3 over type - 1), unless "ios search keyboards activate peck" is
// off. On iOS those keys meant a browser's address or search bar. Here the
// return key type only picks the enter key's icon, so Firefox's address bar
// (go) and every search box came up in peck mode whatever their field rule
// said. With an empty mask the keyboard type alone decides, as the rules set it.
constexpr EnginePatch kPatch{0x006a1a1c, 0xe3a010a3, 0xe3a01000};  // mov r1, #0xa3 -> mov r1, #0

}  // namespace

bool installFieldTypes(Guest& g, const std::vector<uint8_t>& image) {
  if (!installEnginePatches(g, image, &kPatch, 1, "field types")) return false;
  NIN_LOGI("field types: the enter key no longer turns on peck mode");
  return true;
}

}  // namespace nin

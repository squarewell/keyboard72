#pragma once

#include <cstdint>
#include <vector>

namespace nin {
class Guest;

bool jniSyncBypassRequested();
// Boot-time only, before constructors/native exports. Retain the guest's parsed
// JNI metadata across initenv calls; keep explicit resets and class refreshes.
bool installJniSyncBypass(Guest& g, const std::vector<uint8_t>& image);
}  // namespace nin

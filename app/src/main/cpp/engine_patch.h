#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nin {
class Guest;
struct EnginePatch { uint32_t offset, before, after; };
// Boot-time only: validate the complete original ELF and all loaded sites before
// writing any instruction. The on-disk image is never changed.
bool installEnginePatches(Guest& g, const std::vector<uint8_t>& image,
                          const EnginePatch* patches, size_t count, const char* label);
}  // namespace nin

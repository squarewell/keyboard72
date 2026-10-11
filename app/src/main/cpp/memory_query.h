#pragma once

#include <cstdint>
#include <vector>

namespace nin {
class Guest;

// Boot-time only. Answers Shing::getUsedMemory (the memory indicator and the
// logs menu) from this process's real resident and virtual size instead of the
// hidden /proc/self/stat, which left the engine reporting uninitialized stack.
bool installMemoryQuery(Guest& g, const std::vector<uint8_t>& image);
}  // namespace nin

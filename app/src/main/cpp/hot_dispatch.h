#pragma once

#include <cstdint>
#include <vector>

namespace nin {
class Guest;
bool floatDispatchRequested();
bool byteCasRequested();
bool refcountAddRequested();
// Boot-time only, before constructors. All default off and fail closed.
bool installFloatDispatch(Guest& g, const std::vector<uint8_t>& image);
bool installByteCas(Guest& g, const std::vector<uint8_t>& image);
bool installRefcountAdd(Guest& g, const std::vector<uint8_t>& image);
}  // namespace nin

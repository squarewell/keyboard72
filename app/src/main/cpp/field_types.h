#pragma once

#include <cstdint>
#include <vector>

namespace nin {
class Guest;

// Boot-time only. The keyboard's field rules (FieldRules.kt) choose each
// field's board; this stops the engine from also switching to URL peck mode on
// its own whenever the enter key is go or search.
bool installFieldTypes(Guest& g, const std::vector<uint8_t>& image);
}  // namespace nin

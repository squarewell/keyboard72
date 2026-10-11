#pragma once

#include <cstdint>
#include <vector>

namespace nin {
class Guest;

bool diagnosticBypassRequested();
// Boot-time only, after ELF relocation and before constructors/native exports.
// Verifies the original image and every loaded instruction before any write.
bool installDiagnosticBypass(Guest& g, const std::vector<uint8_t>& image);
}  // namespace nin

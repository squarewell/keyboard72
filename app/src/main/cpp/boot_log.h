// Start-up log for the keyboard's boot screen (BootScreen.kt): the shim's
// milestones and the engine's own log lines (with their ANSI colors), recorded
// only between bootLogBegin and bootLogEnd, so it costs nothing while typing.
#pragma once

#include <cstdint>
#include <string>

namespace nin {

// Line kinds: a step of the boot, a detail of the current step, an engine log line.
enum class BootLine : char { Step = 'S', Info = 'I', Engine = 'E' };

void bootLogBegin();
void bootLogEnd();
bool bootLogging();
void bootLog(BootLine kind, const std::string& text);
// Lines numbered `next` and later, one per line as "kind<TAB>ms<TAB>text"
// (ms since bootLogBegin), after a first line "next<TAB>elapsed ms" with the
// number to ask for next and the boot's time so far (or in all, once ended).
std::string bootLogRead(uint32_t next);

}  // namespace nin

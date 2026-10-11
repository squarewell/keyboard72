#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nin {
class Engine;
class Guest;

// The engine's own backup text: what its menus put in a text box and its
// importer reads back (an instruction line, TYPE/DATE, then MAGlCIAN-framed
// chunks). Call with the engine lock held, after the keyboard view has been
// initialized (the engine's Moopad exists only then).
enum class BackupKind { Settings = 0, Words = 1, ReadableWords = 2, Shortcuts = 3 };

// False when the keyboard has not been initialized yet or the kind is unknown.
bool exportBackup(Engine& e, int kind, std::string& out);

// Hands `text` to the engine's importer (the path its "import from clipboard"
// uses), which asks for confirmation in the keyboard. Returns 1 when accepted,
// 0 when the text is not an export, -1 when the keyboard is not initialized.
int importBackup(Engine& e, const std::string& text);

// Boot-time engine patch (hash-guarded): the import prompts open at a fixed
// point that the engine keeps on the keyboard, instead of at its page middle,
// which is unset on Android and put them partly off a short keyboard.
bool installBackupPrompts(Guest& g, const std::vector<uint8_t>& image);
}  // namespace nin

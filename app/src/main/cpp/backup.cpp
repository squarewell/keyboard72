#include "backup.h"

#include <cstring>
#include <vector>

#include "engine.h"
#include "engine_patch.h"
#include "guest.h"
#include "log.h"

namespace nin {
namespace {

// Emkey::Moopad offsets of members the engine's own callers use: the
// ImportationModule (Moopad::launchRestartMenu, createMenu) and the
// SpaceCompleter (ServerMode::handleExportRPC).
constexpr uint32_t kImportationModule = 0x8b60;
constexpr uint32_t kSpaceCompleter = 0x24028;

uint32_t moopad(Guest& g) {
  uint32_t global = g.symbol("_ZN5Emkey13_globalmoopadE");
  return global ? g.r32(global) : 0;
}

// The engine's libstdc++ std::string is copy-on-write: the object holds a
// pointer to the characters, preceded by length, capacity and refcount words.
std::string readString(Guest& g, uint32_t chars) { return std::string(g.cstr(chars), g.r32(chars - 12)); }

uint32_t makeString(Guest& g, const std::string& s) {
  const uint32_t n = uint32_t(s.size());
  uint32_t rep = g.malloc(12 + n + 1);
  g.w32(rep, n);
  g.w32(rep + 4, n);
  g.w32(rep + 8, 0);  // one owner
  std::memcpy(g.host(rep + 12), s.data(), n);
  g.w8(rep + 12 + n, 0);
  return rep + 12;
}

// _Rep::_M_dispose: drop one reference, free the last. Empty strings share a
// static representation and are never freed.
void releaseString(Guest& g, uint32_t chars) {
  const uint32_t rep = chars - 12;
  if (g.r32(rep) == 0 && g.r32(rep + 4) == 0) return;
  const int32_t refs = int32_t(g.r32(rep + 8));
  if (refs <= 0) g.free(rep);
  else g.w32(rep + 8, uint32_t(refs - 1));
}

// Placement of the import prompts. promptOpenAccessImportation and
// promptReadableAccessImportation open their SlideMenu at (-1000, -1000), which
// SlideMenuEnv::activate replaces with RoenGL::pagemid(); that page size is
// never set on Android, and the prompt opened partly below a short keyboard.
// Given a real point instead, the engine places the menu beside it and keeps
// it on the keyboard (checked at several points on a 644 x 560 keyboard). The
// mov/orr pair that builds -1000.0f now builds 100.0f, stored as both
// coordinates, which puts the prompt toward the keyboard's left side.
constexpr uint32_t kMovSentinel = 0xe3a0087a, kOrrSentinel = 0xe3800331;  // mov r0,#0x7a0000; orr r0,r0,#0xc4000000
constexpr uint32_t kMovPoint = 0xe3a00442, kOrrPoint = 0xe38008c8;        // 100.0f: mov r0,#0x42000000; orr r0,r0,#0xc80000
constexpr EnginePatch kPromptPatches[] = {
    {0x00770e70, kMovSentinel, kMovPoint}, {0x00770e78, kOrrSentinel, kOrrPoint},  // promptReadableAccessImportation
    {0x00771e1c, kMovSentinel, kMovPoint}, {0x00771e20, kOrrSentinel, kOrrPoint},  // promptOpenAccessImportation
};

}  // namespace

bool installBackupPrompts(Guest& g, const std::vector<uint8_t>& image) {
  if (!installEnginePatches(g, image, kPromptPatches, 4, "import prompt placement")) return false;
  NIN_LOGI("import prompt placement: prompts open on the keyboard");
  return true;
}

bool exportBackup(Engine& e, int kind, std::string& out) {
  Guest& g = e.guest();
  const uint32_t m = moopad(g);
  if (!m) return false;
  const char* fn = nullptr;
  uint32_t self = m;
  switch (BackupKind(kind)) {
    case BackupKind::Settings: fn = "_ZN5Emkey6Moopad17getSettingsExportEv"; break;
    case BackupKind::Words: fn = "_ZN5Emkey6Moopad13getWordExportEv"; break;
    case BackupKind::ReadableWords: fn = "_ZN5Emkey6Moopad21getReadableWordExportEv"; break;
    case BackupKind::Shortcuts:
      fn = "_ZN5Emkey14SpaceCompleter18getShortcutsExportEv";
      self = m + kSpaceCompleter;
      break;
  }
  if (!fn) return false;
  const uint32_t result = g.malloc(4);  // returned std::string (hidden pointer)
  e.callExport(fn, {result, self});
  const uint32_t chars = g.r32(result);
  out = readString(g, chars);
  releaseString(g, chars);
  g.free(result);
  return true;
}

int importBackup(Engine& e, const std::string& text) {
  Guest& g = e.guest();
  const uint32_t m = moopad(g);
  if (!m) return -1;
  const uint32_t holder = g.malloc(4);
  const uint32_t chars = makeString(g, text);
  g.w32(holder, chars);
  const uint32_t importer = m + kImportationModule;
  int result = 0;
  if (e.callExport("_ZN5Emkey17ImportationModule17canImportFromTextERKSs", {importer, holder}) & 0xff) {
    e.callExport("_ZN5Emkey17ImportationModule10importTextERKSs", {importer, holder});
    result = 1;
  }
  releaseString(g, g.r32(holder));
  g.free(holder);
  NIN_LOGI("backup import: %s (%zu bytes)", result ? "handed to the engine" : "not an export", text.size());
  return result;
}

}  // namespace nin

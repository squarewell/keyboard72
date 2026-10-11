#include "memory_query.h"

#include <algorithm>
#include <cstdio>
#include <unistd.h>

#include "cpu.h"
#include "engine_patch.h"
#include "guest.h"
#include "log.h"

namespace nin {
namespace {

// Shing::getUsedMemory(long& rss, long& vsize). The original parses
// /proc/self/stat with the engine's own iostreams, which cannot work here: /proc
// is hidden from the guest (a 64-bit process's values overflow its 32-bit
// parsing), and a successful open reaches a locale facet lookup that throws
// bad_cast under emulation. Its first instruction becomes a branch to a trap
// that writes the two results and returns.
constexpr uint32_t kGetUsedMemory = 0x00ea5fa4;           // ELF address, ARM code
constexpr uint32_t kPushPrologue = 0xe92d4bf0;             // push {r4-r9, fp, lr}
constexpr uint32_t kTrapStub = kStubs + 0x10000 - 8;       // past the import stubs

constexpr uint32_t armBranch(uint32_t from, uint32_t to) {
  return 0xea000000u | (((to - (from + 8)) >> 2) & 0xffffffu);
}
constexpr EnginePatch kPatch{kGetUsedMemory, kPushPrologue,
                             armBranch(kImageBase + kGetUsedMemory, kTrapStub)};

uint32_t guestLong(unsigned long long bytes) {
  return uint32_t(std::min<unsigned long long>(bytes, 0x7fffffff));
}

void answer(Guest& g) {
  unsigned long long pages = 0, resident = 0;
  if (FILE* f = std::fopen("/proc/self/statm", "r")) {
    if (std::fscanf(f, "%llu %llu", &pages, &resident) != 2) pages = resident = 0;
    std::fclose(f);
  }
  const unsigned long long page = (unsigned long long)sysconf(_SC_PAGESIZE);
  g.w32(g.reg(kR0), guestLong(resident * page));  // RSS in bytes
  g.w32(g.reg(kR1), guestLong(pages * page));     // virtual size, capped
}

}  // namespace

bool installMemoryQuery(Guest& g, const std::vector<uint8_t>& image) {
  if (kStubs + 8 * g.importStubCount() > kTrapStub) {
    NIN_LOGW("memory query refused: no free stub slot");
    return false;
  }
  if (!installEnginePatches(g, image, &kPatch, 1, "memory query")) return false;
  g.addTrapTable(kTrapStub, 1, [](Guest& guest, uint32_t) { answer(guest); });
  NIN_LOGI("memory query: Shing::getUsedMemory answered natively");
  return true;
}

}  // namespace nin

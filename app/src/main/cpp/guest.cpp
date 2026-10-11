#include "guest.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cxxabi.h>
#include <elf.h>
#include <sys/mman.h>

#include "fastlibc_blob.h"
#include "log.h"

namespace nin {

namespace {
const int kCoreRegs[4] = {kR0, kR1, kR2, kR3};
uint32_t alignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }
}  // namespace

// ------------------------------------------------------------------- Args
Args::Args(Guest& g, uint32_t va_list, bool use_va) : g_(g), va_(use_va), vap_(va_list) {
  if (!va_) nsaa_ = g.reg(kSP);
}

uint32_t Args::u32() {
  if (va_) {
    uint32_t v = g_.r32(vap_);
    vap_ += 4;
    return v;
  }
  if (ncrn_ < 4) return g_.reg(kCoreRegs[ncrn_++]);
  uint32_t v = g_.r32(nsaa_);
  nsaa_ += 4;
  return v;
}

uint64_t Args::u64() {
  if (va_) {
    vap_ = (vap_ + 7) & ~7u;
    uint64_t v = g_.r32(vap_) | (uint64_t(g_.r32(vap_ + 4)) << 32);
    vap_ += 8;
    return v;
  }
  if (ncrn_ & 1) ncrn_++;
  if (ncrn_ <= 2) {
    uint64_t v = g_.reg(kCoreRegs[ncrn_]) | (uint64_t(g_.reg(kCoreRegs[ncrn_ + 1])) << 32);
    ncrn_ += 2;
    return v;
  }
  ncrn_ = 4;
  nsaa_ = (nsaa_ + 7) & ~7u;
  uint64_t v = g_.r32(nsaa_) | (uint64_t(g_.r32(nsaa_ + 4)) << 32);
  nsaa_ += 8;
  return v;
}

// ------------------------------------------------------------------ Guest
Guest::Guest() {
  void* p = mmap(nullptr, 1ull << 32, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (p == MAP_FAILED) fatal("cannot reserve 4 GiB of address space for the guest");
  base_ = static_cast<uint8_t*>(p);
  cpu_ = makeCpu(this, base_, kStop);
  NIN_LOGI("cpu backend: %s", cpu_->name());
  for (uint32_t region : {kStubs, kImportData, kJniArea, kStop, kNative}) mapRegion(region, 0x10000);
  mapRegion(kScratch, 0x100000);
  mapRegion(kHeap, kHeapSize);
  mapRegion(kMmap, kMmapSize);
  mapRegion(kStack, kStackSize);
  writeTrap(kStop, kTrapStop);
  installKuser();
}

Guest::~Guest() {
  cpu_.reset();
  if (base_) munmap(base_, 1ull << 32);
}

void Guest::mapRegion(uint32_t g, uint32_t size) {
  if (g % kRegionAlign || size % kRegionAlign) fatal("unaligned guest region");
  if (mprotect(base_ + g, size, PROT_READ | PROT_WRITE) != 0) fatal("mprotect failed for guest region");
  cpu_->mapRegion(g, size);
}

void Guest::writeTrap(uint32_t address, uint32_t number) {
  w32(address, svcInstruction(number));
  w32(address + 4, kBxLr);
}

void Guest::fatal(const std::string& what) {
  NIN_LOGE("FATAL: %s", what.c_str());
  if (depth_ > 0) logGuestBacktrace();
  std::abort();
}

// The engine is built without frame pointers, so this scans the guest stack
// for words that look like return addresses: inside the engine's code and
// right after a call instruction (ARM BL/BLX, Thumb BL/BLX). A heuristic (stale
// frames can show up), but enough to place a crash in a device log.
void Guest::logGuestBacktrace() {
  uint32_t sp = reg(kSP);
  NIN_LOGE("  pc %s | lr %s | sp %#x", symbolize(reg(kPC)).c_str(), symbolize(reg(kLR)).c_str(), sp);
  int shown = 0;
  for (uint32_t a = sp; a < sp + 4096 && a + 4 <= kStack + kStackSize && shown < 16; a += 4) {
    if (a < kStack) continue;
    uint32_t w = r32(a);
    if (w < kImageBase + 8 || w >= imageEnd_) continue;
    bool call = false;
    if (w & 1) {  // Thumb return address: 32-bit BL/BLX or 16-bit BLX Rm before it
      uint32_t t = w & ~1u;
      uint16_t hw1 = uint16_t(r32(t - 4)), hw2 = uint16_t(r32(t - 4) >> 16), last = uint16_t(r32(t - 4) >> 16);
      call = ((hw1 & 0xf800) == 0xf000 && (hw2 & 0xc000) == 0xc000) || (last & 0xff87) == 0x4780;
    } else if (!(w & 3)) {  // ARM: BL/BLX imm, or BLX Rm
      uint32_t insn = r32(w - 4);
      call = (insn & 0x0f000000) == 0x0b000000 || (insn & 0xfe000000) == 0xfa000000 ||
             (insn & 0x0ffffff0) == 0x012fff30;
    }
    if (call) NIN_LOGE("  stack %#x: %s", a, symbolize(w).c_str()), shown++;
  }
}

std::string Guest::stdstring(uint32_t obj) const {
  uint32_t data = r32(obj);
  uint32_t len = r32(data - 12);
  return std::string(cstr(data), len);
}

uint32_t Guest::malloc(uint32_t n) {
  if (!mallocFn_) fatal("guest allocator not installed");
  return static_cast<uint32_t>(call(mallocFn_, {n}));
}

void Guest::free(uint32_t p) {
  if (p) call(freeFn_, {p});
}

uint32_t Guest::allocCString(const std::string& s) { return allocBytes(s.c_str(), uint32_t(s.size() + 1)); }

uint32_t Guest::allocBytes(const void* data, uint32_t n) {
  uint32_t p = malloc(n);
  std::memcpy(host(p), data, n);
  return p;
}

// --------------------------------------------------------------- loading
void Guest::loadElf(const std::vector<uint8_t>& image) {
  auto eh = reinterpret_cast<const Elf32_Ehdr*>(image.data());
  if (image.size() < sizeof(Elf32_Ehdr) || std::memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 ||
      eh->e_ident[EI_CLASS] != ELFCLASS32 || eh->e_machine != EM_ARM)
    fatal("engine image is not a 32-bit ARM ELF");
  auto ph = reinterpret_cast<const Elf32_Phdr*>(image.data() + eh->e_phoff);
  uint32_t end = 0, dyn = 0;
  for (int i = 0; i < eh->e_phnum; i++) {
    if (ph[i].p_type == PT_LOAD) end = std::max(end, ph[i].p_vaddr + ph[i].p_memsz);
    if (ph[i].p_type == PT_DYNAMIC) dyn = ph[i].p_vaddr;
    if (ph[i].p_type == 0x70000001) {  // PT_ARM_EXIDX
      exidx_start_ = kImageBase + ph[i].p_vaddr;
      exidx_count_ = ph[i].p_memsz / 8;
    }
  }
  imageEnd_ = alignUp(kImageBase + end, kRegionAlign);
  if (imageEnd_ > kStubs) fatal("engine image too large for guest layout");
  mapRegion(kImageBase, imageEnd_ - kImageBase);
  for (int i = 0; i < eh->e_phnum; i++)
    if (ph[i].p_type == PT_LOAD)
      std::memcpy(host(kImageBase + ph[i].p_vaddr), image.data() + ph[i].p_offset, ph[i].p_filesz);

  uint32_t symtab = 0, strtab = 0, rel = 0, relsz = 0, jmprel = 0, pltrelsz = 0, hash = 0,
           init = 0, initsz = 0;
  for (uint32_t d = kImageBase + dyn;; d += 8) {
    uint32_t tag = r32(d), val = r32(d + 4);
    if (tag == DT_NULL) break;
    switch (tag) {
      case DT_SYMTAB: symtab = val; break;
      case DT_STRTAB: strtab = val; break;
      case DT_REL: rel = val; break;
      case DT_RELSZ: relsz = val; break;
      case DT_JMPREL: jmprel = val; break;
      case DT_PLTRELSZ: pltrelsz = val; break;
      case DT_HASH: hash = val; break;
      case DT_INIT_ARRAY: init = val; break;
      case DT_INIT_ARRAYSZ: initsz = val; break;
    }
  }
  uint32_t nsyms = r32(kImageBase + hash + 4);
  std::vector<uint32_t> values(nsyms, 0);
  std::vector<std::string> names(nsyms);
  uint32_t stubCursor = kStubs, dataCursor = kImportData;
  std::vector<uint32_t> ifuncs;
  for (uint32_t i = 0; i < nsyms; i++) {
    auto s = hp<Elf32_Sym>(kImageBase + symtab + i * sizeof(Elf32_Sym));
    names[i] = cstr(kImageBase + strtab + s->st_name);
    if (s->st_shndx != SHN_UNDEF) {
      values[i] = kImageBase + s->st_value;
      if (ELF32_ST_TYPE(s->st_info) == STT_GNU_IFUNC) ifuncs.push_back(i);
      if (!names[i].empty()) {
        defined_.emplace(names[i], values[i]);
        if (ELF32_ST_TYPE(s->st_info) == STT_FUNC || ELF32_ST_TYPE(s->st_info) == STT_GNU_IFUNC) {
          uint32_t start = values[i] & ~1u;
          // A zero-sized symbol identifies its entry only, never the following gap.
          sorted_.push_back({start, start + std::max(1u, s->st_size), names[i]});
        }
      }
    } else if (!names[i].empty()) {
      ImportInfo info;
      info.name = names[i];
      if (ELF32_ST_TYPE(s->st_info) == STT_OBJECT) {
        info.data = true;
        info.stub = dataCursor;
        dataCursor += 0x800;
      } else {
        info.stub = stubCursor;
        writeTrap(stubCursor, kTrapImport | uint32_t(stubNames_.size()));
        stubNames_.push_back(names[i]);
        stubThunks_.emplace_back();
        stubCursor += 8;
      }
      values[i] = info.stub;
      imports_.emplace(names[i], std::move(info));
    }
  }
  if (stubCursor > kStubs + 0x10000 || dataCursor > kHeapState) fatal("too many imports");
  std::sort(sorted_.begin(), sorted_.end(), [](auto& a, auto& b) {
    if (a.begin != b.begin) return a.begin < b.begin;
    if (a.end != b.end) return a.end > b.end;
    return a.name < b.name;
  });
  uint32_t maxEnd = 0;
  for (const auto& s : sorted_) {
    maxEnd = std::max(maxEnd, s.end);
    symbolMaxEnd_.push_back(maxEnd);
  }
  // Sections identify PLT/code honestly even when dynamic function names are absent.
  if (eh->e_shentsize == sizeof(Elf32_Shdr) && eh->e_shstrndx < eh->e_shnum &&
      uint64_t(eh->e_shoff) + uint64_t(eh->e_shnum) * sizeof(Elf32_Shdr) <= image.size()) {
    auto sh = reinterpret_cast<const Elf32_Shdr*>(image.data() + eh->e_shoff);
    const auto& strings = sh[eh->e_shstrndx];
    if (uint64_t(strings.sh_offset) + strings.sh_size <= image.size())
      for (uint32_t i = 0; i < eh->e_shnum; i++) {
        if (!(sh[i].sh_flags & SHF_EXECINSTR) || sh[i].sh_name >= strings.sh_size) continue;
        const char* name = reinterpret_cast<const char*>(image.data() + strings.sh_offset + sh[i].sh_name);
        if (!std::memchr(name, 0, strings.sh_size - sh[i].sh_name)) continue;
        uint32_t begin = kImageBase + sh[i].sh_addr, end = begin + sh[i].sh_size;
        codeRegions_.push_back({begin, end, name});
        if (std::strcmp(name, ".plt") == 0) { pltBegin_ = begin; pltEnd_ = end; }
      }
  }
  for (uint32_t i = 0; i < exidx_count_; i++) {
    uint32_t at = exidx_start_ + 8 * i;
    int32_t relative = int32_t(r32(at) << 1) >> 1;  // PREL31, sign extended
    unwindStarts_.push_back((at + uint32_t(relative)) & ~1u);
  }
  std::sort(unwindStarts_.begin(), unwindStarts_.end());
  for (uint32_t off = 0; off < pltrelsz; off += 8) {
    uint32_t sym = r32(kImageBase + jmprel + off + 4) >> 8;
    pltNames_.push_back(sym < names.size() ? names[sym] : "?");
  }

  // GNU indirect functions (__atomic_fetch_add_4, used for the reference counts
  // of every std::string the engine's own code frees): the symbol is a resolver
  // that returns the implementation's address. Like Android's linker, call it
  // once (argument: AT_HWCAP of an ARMv7 CPU with VFPv3/NEON/IDIV) and relocate
  // against the result. Linking the resolver itself makes each call a no-op, so
  // no string was ever freed and the heap ran out after a few minutes.
  constexpr uint32_t kHwcap = 0x7b0d7;
  for (uint32_t i : ifuncs) {
    uint32_t resolved = uint32_t(call(values[i], {kHwcap}));
    NIN_LOGI("ifunc %s -> %s", names[i].c_str(), symbolize(resolved).c_str());
    values[i] = resolved;
    defined_[names[i]] = resolved;
  }

  auto relocate = [&](uint32_t table, uint32_t size) {
    for (uint32_t off = 0; off < size; off += 8) {
      uint32_t where = kImageBase + r32(kImageBase + table + off);
      uint32_t info = r32(kImageBase + table + off + 4);
      uint32_t type = info & 0xff, sym = info >> 8;
      uint32_t addend = r32(where), value = sym < nsyms ? values[sym] : 0;
      switch (type) {
        case R_ARM_NONE: continue;
        case R_ARM_RELATIVE: w32(where, kImageBase + addend); break;
        case R_ARM_GLOB_DAT:
        case R_ARM_JUMP_SLOT: w32(where, value); break;
        case R_ARM_ABS32: w32(where, value + addend); break;
        default: fatal("unsupported relocation type " + std::to_string(type));
      }
      if (sym == 0 || sym >= nsyms || type == R_ARM_RELATIVE) continue;
      auto it = imports_.find(names[sym]);
      if (it != imports_.end() && !it->second.data) it->second.sites.push_back(where);
    }
  };
  relocate(rel, relsz);
  relocate(jmprel, pltrelsz);
  for (uint32_t i = 0; i < initsz / 4; i++) initArray_.push_back(r32(kImageBase + init + 4 * i));
  NIN_LOGI("engine loaded: %zu imports, %zu symbols, %zu constructors", imports_.size(),
           defined_.size(), initArray_.size());
}

void Guest::installNativeBlob() {
  std::memcpy(host(kNative), kFastlibcBlob, sizeof(kFastlibcBlob));
  std::memset(host(kHeapState), 0, 4 * (4 + 4096 / 16 + 1));
  w32(kHeapState, kHeap);
  w32(kHeapState + 4, kHeap + kHeapSize);
  mallocFn_ = kFastlibcMalloc;
  freeFn_ = kFastlibcFree;
  if (!std::getenv("NIN_SOFT_FLOAT")) installHardFloat();
}

// The engine was built for soft-float armeabi: every float operation calls a
// libgcc routine of 20-60 integer instructions (redirecting them removes ~20%
// of the instructions executed while typing). Branch each routine to its VFP equivalent in
// native/vfpfloat.c, which keeps the soft-float calling convention and returns
// the same bits.
// Aliases share an address (__eqsf2/__nesf2 = __cmpsf2, __ltsf2 = __lesf2, ...).
void Guest::installHardFloat(bool redirect) {
  auto branch = [](uint32_t from, uint32_t to, uint32_t& insn) {  // ARM `b`, +-32 MiB
    int64_t offset = int64_t(to) - int64_t(from) - 8;
    if ((from | to) & 3 || offset < -(1 << 25) || offset >= (1 << 25)) return false;
    insn = 0xea000000u | ((uint32_t(offset) >> 2) & 0xffffffu);
    return true;
  };
  // Trampolines to the original __aeabi_fadd/__aeabi_dadd for the few inputs
  // where that libgcc's rounding differs from IEEE (see vfpfloat.c): the
  // routine's first instruction (known, position-independent), then a branch
  // to its second.
  const struct { const char* name; uint32_t first, trampoline; } originals[] = {
      {"__aeabi_fadd", 0xe1b02080 /* lsls r2, r0, #1 */, kFastlibcSoftFadd},
      {"__aeabi_dadd", 0xe92d4030 /* push {r4, r5, lr} */, kFastlibcSoftDadd},
  };
  bool haveOriginals = true;
  for (auto& o : originals) {
    uint32_t entry = symbol(o.name), jump = 0;
    if (!entry || r32(entry) != o.first || !branch(o.trampoline + 4, entry + 4, jump)) {
      haveOriginals = false;
      continue;
    }
    w32(o.trampoline, o.first);
    w32(o.trampoline + 4, jump);
    cpu_->invalidateCode(o.trampoline, 8);
  }
  if (!redirect) return;
  const std::pair<const char*, uint32_t> routines[] = {
      {"__aeabi_fadd", kFastlibcFadd}, {"__aeabi_fsub", kFastlibcFsub}, {"__aeabi_frsub", kFastlibcFrsub},
      {"__aeabi_fmul", kFastlibcFmul}, {"__aeabi_fdiv", kFastlibcFdiv},
      {"__aeabi_fcmpeq", kFastlibcFcmpeq}, {"__aeabi_fcmplt", kFastlibcFcmplt},
      {"__aeabi_fcmple", kFastlibcFcmple}, {"__aeabi_fcmpge", kFastlibcFcmpge},
      {"__aeabi_fcmpgt", kFastlibcFcmpgt}, {"__aeabi_fcmpun", kFastlibcFcmpun},
      {"__cmpsf2", kFastlibcCmpsf2}, {"__lesf2", kFastlibcCmpsf2}, {"__gesf2", kFastlibcGesf2},
      {"__aeabi_cfcmple", kFastlibcCfcmple}, {"__aeabi_cfrcmple", kFastlibcCfrcmple},
      {"__aeabi_i2f", kFastlibcI2f}, {"__aeabi_ui2f", kFastlibcUi2f},
      {"__aeabi_f2iz", kFastlibcF2iz}, {"__aeabi_f2uiz", kFastlibcF2uiz},
      {"__aeabi_l2f", kFastlibcL2f}, {"__aeabi_ul2f", kFastlibcUl2f},
      {"__aeabi_dadd", kFastlibcDadd}, {"__aeabi_dsub", kFastlibcDsub}, {"__aeabi_drsub", kFastlibcDrsub},
      {"__aeabi_dmul", kFastlibcDmul}, {"__aeabi_ddiv", kFastlibcDdiv},
      {"__aeabi_dcmpeq", kFastlibcDcmpeq}, {"__aeabi_dcmplt", kFastlibcDcmplt},
      {"__aeabi_dcmple", kFastlibcDcmple}, {"__aeabi_dcmpge", kFastlibcDcmpge},
      {"__aeabi_dcmpgt", kFastlibcDcmpgt}, {"__aeabi_dcmpun", kFastlibcDcmpun},
      {"__cmpdf2", kFastlibcCmpdf2}, {"__ledf2", kFastlibcCmpdf2}, {"__gedf2", kFastlibcGedf2},
      {"__aeabi_cdcmple", kFastlibcCdcmple}, {"__aeabi_cdrcmple", kFastlibcCdrcmple},
      {"__aeabi_i2d", kFastlibcI2d}, {"__aeabi_ui2d", kFastlibcUi2d},
      {"__aeabi_d2iz", kFastlibcD2iz}, {"__aeabi_d2uiz", kFastlibcD2uiz},
      {"__aeabi_f2d", kFastlibcF2d}, {"__aeabi_d2f", kFastlibcD2f},
      {"__aeabi_l2d", kFastlibcL2d}, {"__aeabi_ul2d", kFastlibcUl2d},
  };
  const std::string addSub[] = {"__aeabi_fadd", "__aeabi_fsub", "__aeabi_frsub",
                                "__aeabi_dadd", "__aeabi_dsub", "__aeabi_drsub"};
  int patched = 0;
  for (auto& [name, target] : routines) {
    uint32_t from = symbol(name), jump = 0;
    bool needsOriginal = std::find(std::begin(addSub), std::end(addSub), name) != std::end(addSub);
    if (!from || !branch(from, target, jump) || (needsOriginal && !haveOriginals)) {
      NIN_LOGW("keeping soft-float %s", name);
      continue;
    }
    w32(from, jump);
    cpu_->invalidateCode(from, 4);
    patched++;
  }
  NIN_LOGI("hardware floating point: %d routines redirected", patched);
}

void Guest::runConstructors() {
  for (uint32_t fn : initArray_)
    if (fn != 0 && fn != 0xffffffff) call(fn, {});
}

uint32_t Guest::symbol(const std::string& mangled) const {
  auto it = defined_.find(mangled);
  return it == defined_.end() ? 0 : it->second;
}

uint32_t Guest::importData(const std::string& name) const {
  auto it = imports_.find(name);
  return it == imports_.end() ? 0 : it->second.stub;
}

const Guest::SymbolRange* Guest::findSymbol(uint32_t addr) const {
  addr &= ~1u;
  auto it = std::upper_bound(sorted_.begin(), sorted_.end(), addr,
                             [](uint32_t a, auto& s) { return a < s.begin; });
  size_t i = size_t(it - sorted_.begin());
  const SymbolRange* best = nullptr;
  while (i > 0 && symbolMaxEnd_[i - 1] > addr) {
    const auto& s = sorted_[--i];
    if (addr >= s.begin && addr < s.end &&
        (!best || s.end - s.begin < best->end - best->begin ||
         (s.end - s.begin == best->end - best->begin && s.name < best->name))) best = &s;
  }
  return best;
}

std::pair<uint32_t, std::string> Guest::nearestSymbol(uint32_t addr) const {
  addr &= ~1u;
  auto demangle = [](const std::string& name) {
    int status = 0;
    char* dem = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
    std::string result = status == 0 && dem ? dem : name;
    std::free(dem);
    return result;
  };
  if (auto s = findSymbol(addr)) return {s->begin, demangle(s->name)};
  for (const char* name : {"__atomic_fetch_add_4", "__atomic_add_fetch_4"}) {
    auto it = defined_.find(name);
    if (it != defined_.end() && addr == (it->second & ~1u))
      return {addr, std::string("[resolved ") + name + " entry; extent unknown]"};
  }
  if (addr >= kStubs && (addr - kStubs) / 8 < stubNames_.size()) {
    uint32_t i = (addr - kStubs) / 8;
    const auto& name = stubNames_[i];
    return {kStubs + 8 * i, "[" + std::string(name.compare(0, 2, "gl") == 0 ? "gles" : "import") + " " + name + "]"};
  }
  if (addr >= pltBegin_ && addr < pltEnd_) {
    if (addr >= pltBegin_ + 20 && (pltEnd_ - pltBegin_ - 20) / 12 == pltNames_.size()) {
      uint32_t i = (addr - pltBegin_ - 20) / 12;
      return {pltBegin_ + 20 + i * 12, "[PLT " + demangle(pltNames_[i]) + "]"};
    }
    return {pltBegin_, "[PLT resolver/unknown entry]"};
  }
  if (addr >= kNative && addr < kNative + sizeof(kFastlibcBlob)) return {kNative, "[native code]"};
  if (addr >= kJniArea && addr < kJniArea + 0x10000) return {addr & ~7u, "[jni table stub]"};
  if (addr >= kKuser) return {kKuser, "[kuser helpers]"};
  std::string region = "unmapped/unknown";
  uint32_t begin = addr & ~0xfffu;
  for (const auto& r : codeRegions_) if (addr >= r.begin && addr < r.end) {
    region = r.name;
    auto it = std::upper_bound(unwindStarts_.begin(), unwindStarts_.end(), addr);
    if (it != unwindStarts_.begin() && *--it >= r.begin) begin = *it;
  }
  char buf[64];
  std::snprintf(buf, sizeof buf, " interval @%#x]", begin);
  return {begin, "[anonymous " + region + buf};
}

std::string Guest::symbolize(uint32_t addr) const {
  auto s = nearestSymbol(addr);
  char buf[32];
  std::snprintf(buf, sizeof buf, "+%#x", (addr & ~1u) - s.first);
  return s.second + buf;
}

void Guest::setThunk(const std::string& import, Thunk t) {
  auto it = imports_.find(import);
  if (it == imports_.end() || it->second.data) return;  // engine does not import it
  stubThunks_[(it->second.stub - kStubs) / 8] = std::move(t);
}

void Guest::routeToNative(const std::string& import, uint32_t guest_addr) {
  auto it = imports_.find(import);
  if (it == imports_.end()) return;
  for (uint32_t where : it->second.sites) w32(where, guest_addr);
}

void Guest::addTrapTable(uint32_t address, uint32_t count, TrapHandler h) {
  uint32_t group = kTrapTableBase + (uint32_t(trapTables_.size()) << 16);
  if (group >= kTrapTableEnd) fatal("too many trap tables");
  trapTables_.push_back(std::move(h));
  for (uint32_t i = 0; i < count; i++) writeTrap(address + 8 * i, group | i);
}

// ------------------------------------------------------------- execution
uint64_t Guest::call(uint32_t addr, const std::vector<uint32_t>& args) {
  const bool nested = depth_ > 0;
  uint32_t sp = nested ? (reg(kSP) - 0x400) & ~7u : kStack + kStackSize - 0x100;
  cpu_->enter();
  depth_++;
  if (args.size() > 4) {
    sp -= 4 * uint32_t(args.size() - 4);
    sp &= ~7u;
    for (size_t i = 4; i < args.size(); i++) w32(sp + 4 * uint32_t(i - 4), args[i]);
  }
  for (size_t i = 0; i < 4; i++) setReg(kCoreRegs[i], i < args.size() ? args[i] : 0);
  setReg(kSP, sp);
  setReg(kLR, kStop);
  bool ok;
  ok = cpu_->run(addr);
  if (!ok || !pendingError_.empty()) fatal(pendingError_.empty() ? "guest execution failed" : pendingError_);
  uint64_t result = reg(kR0) | (uint64_t(reg(kR1)) << 32);
  depth_--;
  cpu_->leave();
  return result;
}

void Guest::onFault(const std::string& what) {
  if (pendingError_.empty()) pendingError_ = what;
}

void Guest::onTrap(uint32_t number) {
  if (number == kTrapStop) return cpu_->stop();
  uint32_t group = number & 0xff0000u, index = number & 0xffffu;
  if (group == kTrapImport) return onImport(index);
  size_t table = (group - kTrapTableBase) >> 16;
  if (group >= kTrapTableBase && table < trapTables_.size()) return trapTables_[table](*this, index);
  fatal("unknown trap " + std::to_string(number) + " at " + symbolize(reg(kPC)));
}

void Guest::onImport(uint32_t index) {
  if (index >= stubThunks_.size()) fatal("bad import trap");
  importCalls++;
  static const bool trace = std::getenv("NIN_TRACE_IMPORTS") != nullptr;
  if (trace) NIN_LOGI("import %s from %#x", stubNames_[index].c_str(), reg(kLR));
  Thunk& t = stubThunks_[index];
  if (!t) fatal("unimplemented import " + stubNames_[index] + " called from " + symbolize(reg(kLR)));
  Args a(*this);
  t(*this, a);
}

// Linux ARM kuser helpers used by old armeabi atomics (std::string reference
// counts, guard variables), as plain guest code rather than traps: one core
// that nothing preempts needs no exclusives or barriers.
void Guest::installKuser() {
  mapRegion(kKuser, 0x10000);
  auto code = [&](uint32_t at, std::initializer_list<uint32_t> words) {
    for (uint32_t w : words) w32(at, w), at += 4;
  };
  code(kKuser + 0xf60, {  // __kuser_cmpxchg64(const int64_t* old, const int64_t* new, int64_t* ptr)
      0xe92d00f0,         // push {r4-r7}
      0xe1c040d0,         // ldrd r4, r5, [r0]
      0xe1c260d0,         // ldrd r6, r7, [r2]
      0xe0244006,         // eor r4, r4, r6
      0xe0255007,         // eor r5, r5, r7
      0xe1943005,         // orrs r3, r4, r5
      0x01c140d0,         // ldrdeq r4, r5, [r1]
      0x01c240f0,         // strdeq r4, r5, [r2]
      0xe8bd00f0,         // pop {r4-r7}
      0xe2730000,         // rsbs r0, r3, #0   (r0 = 0 and C set on success)
      kBxLr});
  code(kKuser + 0xfa0, {kBxLr});  // __kuser_memory_barrier
  code(kKuser + 0xfc0, {           // __kuser_cmpxchg(old, new, ptr)
      0xe5923000,                  // ldr r3, [r2]
      0xe0533000,                  // subs r3, r3, r0
      0x05821000,                  // streq r1, [r2]
      0xe2730000,                  // rsbs r0, r3, #0
      kBxLr});
  code(kKuser + 0xfe0, {0xe59f0008, kBxLr});  // __kuser_get_tls: ldr r0, [pc, #8] (word at 0xff0: 0)
  w32(kKuser + 0xffc, 5);                     // __kuser_helper_version
}

}  // namespace nin

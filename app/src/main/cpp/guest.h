// Guest side of the ARM32 compatibility shim: the original 32-bit libgl2jni.so
// runs unmodified on an ARM32 CPU backend (cpu.h: dynarmic or Unicorn), in a
// 4 GiB guest address space that is backed
// 1:1 by one host reservation (guest address g == host address base + g). That
// makes pointer translation an addition and lets real file mappings back guest
// memory. Guest imports (bionic, liblog, libandroid, GLES2, JNI) are served by
// host "thunks"; see bionic_hle.cpp, gles_bridge.cpp, jni_bridge.cpp.
#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cpu.h"

namespace nin {

// Guest memory layout.
constexpr uint32_t kImageBase = 0x00010000;   // ELF vaddr + 0x10000
constexpr uint32_t kStubs = 0x02000000;       // one 8-byte stub per imported function
constexpr uint32_t kImportData = 0x02010000;  // imported data objects (__sF, _ctype_...)
constexpr uint32_t kHeapState = 0x0201e000;   // guest allocator state (native/fastlibc.c)
constexpr uint32_t kClockWords = 0x0201f000;  // unused on device (real clock)
constexpr uint32_t kJniArea = 0x02100000;     // JNIEnv/JavaVM tables and stubs
constexpr uint32_t kStop = 0x02200000;        // return address that ends a host->guest call
constexpr uint32_t kNative = 0x02300000;      // guest-native fast libc blob
constexpr uint32_t kScratch = 0x02400000;     // host-owned guest scratch (strings etc.)
constexpr uint32_t kHeap = 0x10000000;
constexpr uint32_t kHeapSize = 0x40000000;
constexpr uint32_t kMmap = 0x50000000;
constexpr uint32_t kMmapSize = 0x08000000;
constexpr uint32_t kStack = 0x60000000;
constexpr uint32_t kStackSize = 0x01000000;
constexpr uint32_t kKuser = 0xffff0000;
constexpr uint32_t kRegionAlign = 0x10000;    // >= any host page size (4K/16K/64K)

class Guest;

// AAPCS (soft-float) argument reader: core registers then stack, or a va_list.
class Args {
 public:
  Args(Guest& g, uint32_t va_list = 0, bool use_va = false);
  uint32_t u32();
  int32_t i32() { return static_cast<int32_t>(u32()); }
  uint64_t u64();
  int64_t i64() { return static_cast<int64_t>(u64()); }
  float f32() { uint32_t w = u32(); float f; std::memcpy(&f, &w, 4); return f; }
  double f64() { uint64_t w = u64(); double d; std::memcpy(&d, &w, 8); return d; }
  uint32_t ptr() { return u32(); }
 private:
  Guest& g_;
  bool va_;
  uint32_t vap_;
  int ncrn_ = 0;
  uint32_t nsaa_ = 0;
};

using Thunk = std::function<void(Guest&, Args&)>;

struct ImportInfo {
  std::string name;
  uint32_t stub = 0;
  bool data = false;
  std::vector<uint32_t> sites;  // relocated words holding the import's address
};

class Guest : public CpuHost {
 public:
  Guest();
  ~Guest() override;
  Guest(const Guest&) = delete;
  Guest& operator=(const Guest&) = delete;

  // --- memory ------------------------------------------------------------
  uint8_t* host(uint32_t g) const { return base_ + g; }
  template <class T> T* hp(uint32_t g) const { return reinterpret_cast<T*>(base_ + g); }
  uint32_t guestAddr(const void* h) const {
    return static_cast<uint32_t>(reinterpret_cast<const uint8_t*>(h) - base_);
  }
  bool isGuestHost(const void* h) const {
    auto p = reinterpret_cast<const uint8_t*>(h);
    return p >= base_ && p < base_ + (1ull << 32);
  }
  void mapRegion(uint32_t g, uint32_t size);
  uint32_t r32(uint32_t g) const { uint32_t v; std::memcpy(&v, host(g), 4); return v; }
  void w32(uint32_t g, uint32_t v) { std::memcpy(host(g), &v, 4); }
  void w8(uint32_t g, uint8_t v) { *host(g) = v; }
  uint16_t r16(uint32_t g) const { uint16_t v; std::memcpy(&v, host(g), 2); return v; }
  float rf32(uint32_t g) const { float v; std::memcpy(&v, host(g), 4); return v; }
  const char* cstr(uint32_t g) const { return g ? reinterpret_cast<const char*>(host(g)) : nullptr; }
  std::string str(uint32_t g) const { return g ? std::string(cstr(g)) : std::string(); }
  std::string stdstring(uint32_t obj) const;  // GNU COW std::string object

  // Guest heap (guest-native allocator from native/fastlibc.c).
  uint32_t malloc(uint32_t n);
  uint32_t heapUsed() const { return r32(kHeapState) - kHeap; }  // bytes below the allocator's top
  void free(uint32_t p);
  uint32_t allocCString(const std::string& s);
  uint32_t allocBytes(const void* data, uint32_t n);

  // --- loading -----------------------------------------------------------
  void loadElf(const std::vector<uint8_t>& image);
  void installNativeBlob();  // also installHardFloat() unless NIN_SOFT_FLOAT is set
  // Branch the engine's soft-float routines to VFP code (redirect=false only
  // prepares the trampolines back to the originals).
  void installHardFloat(bool redirect = true);
  void runConstructors();
  uint32_t symbol(const std::string& mangled) const;  // defined function address (0 if none)
  std::string symbolize(uint32_t addr) const;
  std::pair<uint32_t, std::string> nearestSymbol(uint32_t addr) const;
  const std::map<std::string, ImportInfo>& imports() const { return imports_; }
  uint32_t importData(const std::string& name) const;
  void setThunk(const std::string& import, Thunk t);
  void routeToNative(const std::string& import, uint32_t guest_addr);
  std::pair<uint32_t, uint32_t> exidx() const { return {exidx_start_, exidx_count_}; }
  uint32_t imageEnd() const { return imageEnd_; }

  // --- execution ---------------------------------------------------------
  // Call guest code (AAPCS words). Re-entrant: thunks may call back in.
  uint64_t call(uint32_t addr, const std::vector<uint32_t>& args);
  const char* cpuName() const { return cpu_->name(); }
  uint32_t reg(int r) const { return cpu_->reg(r); }
  void setReg(int r, uint32_t v) { cpu_->setReg(r, v); }
  uint64_t dreg(int n) const { return cpu_->dreg(n); }
  void setDreg(int n, uint64_t v) { cpu_->setDreg(n, v); }
  void ret32(uint32_t v) { setReg(kR0, v); }
  void ret64(uint64_t v) { setReg(kR0, uint32_t(v)); setReg(kR1, uint32_t(v >> 32)); }
  void retF(float f) { uint32_t w; std::memcpy(&w, &f, 4); ret32(w); }
  void retD(double d) { uint64_t w; std::memcpy(&w, &d, 8); ret64(w); }
  [[noreturn]] void fatal(const std::string& what);

  // A table of `count` 8-byte trap stubs at `address` (e.g. JNIEnv functions);
  // calling stub i invokes handler(guest, i).
  using TrapHandler = std::function<void(Guest&, uint32_t index)>;
  void addTrapTable(uint32_t address, uint32_t count, TrapHandler h);
  size_t importStubCount() const { return stubNames_.size(); }

  // CpuHost
  void onTrap(uint32_t number) override;
  void onFault(const std::string& what) override;
  std::string describe(uint32_t address) const override { return symbolize(address); }

  uint32_t importCalls = 0;

 private:
  void onImport(uint32_t index);
  void installKuser();
  void logGuestBacktrace();
  void writeTrap(uint32_t address, uint32_t number);
  struct SymbolRange { uint32_t begin, end; std::string name; };
  const SymbolRange* findSymbol(uint32_t addr) const;

  uint8_t* base_ = nullptr;
  std::unique_ptr<Cpu> cpu_;
  std::map<std::string, ImportInfo> imports_;
  std::vector<std::string> stubNames_;  // index (address - kStubs)/8
  std::vector<Thunk> stubThunks_;
  std::map<std::string, uint32_t> defined_;           // mangled name -> address
  std::vector<SymbolRange> sorted_;  // ELF FUNC/IFUNC extents, including aliases
  std::vector<uint32_t> symbolMaxEnd_;  // prefix max, for overlapping extents
  std::vector<SymbolRange> codeRegions_;
  std::vector<uint32_t> unwindStarts_;
  uint32_t pltBegin_ = 0, pltEnd_ = 0;
  std::vector<std::string> pltNames_;
  std::vector<uint32_t> initArray_;
  uint32_t exidx_start_ = 0, exidx_count_ = 0;
  uint32_t imageEnd_ = 0;
  uint32_t mallocFn_ = 0, freeFn_ = 0;
  int depth_ = 0;
  std::string pendingError_;
  std::vector<TrapHandler> trapTables_;
};

}  // namespace nin

// dynarmic backend: the ARM32 JIT used by the Citra/Azahar and yuzu/Eden
// emulators. Guest memory is reached through a page table that points straight
// into the host reservation, so loads and stores compile to a few host
// instructions (Unicorn's softmmu costs ~8-10 ns per access).
//
// Jit::Run cannot be re-entered, so nested calls (a trap handler calling back
// into guest code, e.g. the guest allocator) run on a second Jit instance that
// shares the page table; each nesting level has its own register state.
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>
#include <dynarmic/interface/exclusive_monitor.h>

#include "cpu.h"
#include "log.h"

namespace nin {
namespace {

using Dynarmic::A32::Exception;
using Dynarmic::A32::VAddr;
constexpr size_t kPageBits = 12;

std::atomic<bool> g_appJitFastmem{false};
std::atomic<bool> g_jitFastmemActive{false};

bool jitFastmemSetting() {
  if (const char* value = std::getenv("NIN_JIT_FASTMEM")) return std::strcmp(value, "1") == 0;
#ifdef __ANDROID__
  // The app's setting, or the debug property set to 1 (as before the setting).
  char value[PROP_VALUE_MAX] = {};
  __system_property_get("debug.nin.jit_fastmem", value);
  return g_appJitFastmem.load() || std::strcmp(value, "1") == 0;
#else
  return false;
#endif
}

bool jitFastmemRequested() {
  const bool on = jitFastmemSetting();
  if (on) NIN_LOGI("JIT fastmem enabled");
  g_jitFastmemActive = on;
  return on;
}

class DynarmicCpu;

class Callbacks final : public Dynarmic::A32::UserCallbacks {
 public:
  Callbacks(DynarmicCpu& cpu, CpuHost* host) : cpu_(cpu), host_(host) {}

  template <class T>
  T read(VAddr a);
  template <class T>
  void write(VAddr a, T v);

  std::uint8_t MemoryRead8(VAddr a) override { return read<std::uint8_t>(a); }
  std::uint16_t MemoryRead16(VAddr a) override { return read<std::uint16_t>(a); }
  std::uint32_t MemoryRead32(VAddr a) override { return read<std::uint32_t>(a); }
  std::uint64_t MemoryRead64(VAddr a) override { return read<std::uint64_t>(a); }
  void MemoryWrite8(VAddr a, std::uint8_t v) override { write(a, v); }
  void MemoryWrite16(VAddr a, std::uint16_t v) override { write(a, v); }
  void MemoryWrite32(VAddr a, std::uint32_t v) override { write(a, v); }
  void MemoryWrite64(VAddr a, std::uint64_t v) override { write(a, v); }
  template <class T>
  bool exclusive(VAddr a, T v, T expected) {
    if (read<T>(a) != expected) return false;
    write(a, v);
    return true;
  }
  bool MemoryWriteExclusive8(VAddr a, std::uint8_t v, std::uint8_t e) override { return exclusive(a, v, e); }
  bool MemoryWriteExclusive16(VAddr a, std::uint16_t v, std::uint16_t e) override { return exclusive(a, v, e); }
  bool MemoryWriteExclusive32(VAddr a, std::uint32_t v, std::uint32_t e) override { return exclusive(a, v, e); }
  bool MemoryWriteExclusive64(VAddr a, std::uint64_t v, std::uint64_t e) override { return exclusive(a, v, e); }

  void InterpreterFallback(VAddr pc, size_t n) override;
  void CallSVC(std::uint32_t swi) override { host_->onTrap(swi); }
  void ExceptionRaised(VAddr pc, Exception e) override;
  void AddTicks(std::uint64_t) override {}
  std::uint64_t GetTicksRemaining() override { return 1ull << 40; }

 private:
  DynarmicCpu& cpu_;
  CpuHost* host_;
};

class DynarmicCpu : public Cpu {
 public:
  DynarmicCpu(CpuHost* host, uint8_t* base) : host_(host), base_(base), callbacks_(*this, host), monitor_(1) {
    pageTable_ = std::make_unique<std::array<std::uint8_t*, Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES>>();
    pageTable_->fill(nullptr);
  }

  const char* name() const override { return "dynarmic"; }

  void mapRegion(uint32_t g, uint32_t size) override {
    for (uint32_t p = g >> kPageBits; p < ((uint64_t(g) + size) >> kPageBits); p++)
      (*pageTable_)[p] = base_ + (uint64_t(p) << kPageBits);
  }
  uint8_t* hostPage(VAddr a) const { return (*pageTable_)[a >> kPageBits]; }

  uint32_t reg(int r) override { return r == kCPSR ? jit().Cpsr() : jit().Regs()[size_t(r)]; }
  void setReg(int r, uint32_t v) override {
    if (r == kCPSR) jit().SetCpsr(v);
    else jit().Regs()[size_t(r)] = v;
  }
  uint64_t dreg(int n) override {
    auto& e = jit().ExtRegs();
    return e[size_t(2 * n)] | (uint64_t(e[size_t(2 * n + 1)]) << 32);
  }
  void setDreg(int n, uint64_t v) override {
    auto& e = jit().ExtRegs();
    e[size_t(2 * n)] = uint32_t(v);
    e[size_t(2 * n + 1)] = uint32_t(v >> 32);
  }

  void enter() override {
    depth_++;
    if (size_t(depth_) > jits_.size()) {
      Dynarmic::A32::UserConfig config;
      config.callbacks = &callbacks_;
      config.page_table = pageTable_.get();
      // Keep the page table and fault recompilation fallback. Exclusive accesses
      // retain their ordinary path; fastmem_exclusive_access is not fully accurate.
      if (fastmem_) config.fastmem_pointer = reinterpret_cast<uintptr_t>(base_);
      config.global_monitor = &monitor_;
      config.processor_id = 0;
      config.enable_cycle_counting = false;
      config.define_unpredictable_behaviour = true;
      config.code_cache_size = depth_ == 1 ? (128u << 20) : (16u << 20);
      jits_.push_back(std::make_unique<Dynarmic::A32::Jit>(config));
      jits_.back()->SetFpscr(0);  // IEEE, round to nearest (Linux default)
    }
  }
  void leave() override { depth_--; }

  bool run(uint32_t pc) override {
    auto& j = jit();
    faulted_ = false;
    j.Regs()[15] = pc & ~1u;
    j.SetCpsr((j.Cpsr() & ~0x20u) | ((pc & 1) ? 0x20u : 0) | 0x10u);  // T bit, user mode
    j.ClearHalt(Dynarmic::HaltReason::UserDefined1);
    j.Run();
    return !faulted_;
  }
  void stop() override { jit().HaltExecution(Dynarmic::HaltReason::UserDefined1); }

  void invalidateCode(uint32_t a, uint32_t n) override {
    for (auto& j : jits_) j->InvalidateCacheRange(a, n);
  }

  void fault(const std::string& what) {
    if (!faulted_) host_->onFault(what + " at " + host_->describe(jit().Regs()[15]) + " (lr " +
                                  host_->describe(jit().Regs()[14]) + ")");
    faulted_ = true;
    jit().HaltExecution(Dynarmic::HaltReason::UserDefined2);
  }

 private:
  Dynarmic::A32::Jit& jit() { return *jits_[size_t(depth_ - 1)]; }

  CpuHost* host_;
  uint8_t* base_;
  const bool fastmem_ = jitFastmemRequested();
  Callbacks callbacks_;
  Dynarmic::ExclusiveMonitor monitor_;
  std::unique_ptr<std::array<std::uint8_t*, Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES>> pageTable_;
  std::vector<std::unique_ptr<Dynarmic::A32::Jit>> jits_;
  int depth_ = 0;
  bool faulted_ = false;
};

template <class T>
T Callbacks::read(VAddr a) {
  // Reached for code fetches, page-crossing accesses and unmapped pages.
  T v{};
  for (size_t i = 0; i < sizeof(T); i++) {
    uint8_t* page = cpu_.hostPage(VAddr(a + i));
    if (!page) {
      char buf[64];
      std::snprintf(buf, sizeof buf, "invalid memory read addr=%#x size=%zu", a, sizeof(T));
      cpu_.fault(buf);
      return T{};
    }
    reinterpret_cast<uint8_t*>(&v)[i] = page[(a + i) & ((1u << kPageBits) - 1)];
  }
  return v;
}

template <class T>
void Callbacks::write(VAddr a, T v) {
  for (size_t i = 0; i < sizeof(T); i++) {
    uint8_t* page = cpu_.hostPage(VAddr(a + i));
    if (!page) {
      char buf[64];
      std::snprintf(buf, sizeof buf, "invalid memory write addr=%#x size=%zu", a, sizeof(T));
      cpu_.fault(buf);
      return;
    }
    page[(a + i) & ((1u << kPageBits) - 1)] = reinterpret_cast<uint8_t*>(&v)[i];
  }
}

void Callbacks::InterpreterFallback(VAddr pc, size_t n) {
  char buf[96];
  std::snprintf(buf, sizeof buf, "dynarmic cannot translate %zu instruction(s) at %#x", n, pc);
  cpu_.fault(buf);
}

void Callbacks::ExceptionRaised(VAddr pc, Exception e) {
  switch (e) {
    case Exception::Yield:
    case Exception::SendEvent:
    case Exception::SendEventLocal:
    case Exception::WaitForEvent:
    case Exception::WaitForInterrupt:
    case Exception::PreloadData:
    case Exception::PreloadDataWithIntentToWrite:
    case Exception::PreloadInstruction:
      return;  // hints: no-ops for a single emulated core
    default: {
      char buf[96];
      std::snprintf(buf, sizeof buf, "CPU exception %d at %#x", int(e), pc);
      cpu_.fault(buf);
    }
  }
}

}  // namespace

void setAppJitFastmem(bool on) { g_appJitFastmem = on; }
bool jitFastmemActive() { return g_jitFastmemActive; }

std::unique_ptr<Cpu> makeCpu(CpuHost* host, uint8_t* base, uint32_t) {
  return std::make_unique<DynarmicCpu>(host, base);
}

}  // namespace nin

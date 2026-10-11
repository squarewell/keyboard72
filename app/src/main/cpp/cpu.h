// CPU backend used by Guest: executes ARM32 code in the 4 GiB guest space.
//
// Guest memory is one host reservation (host = base + guest address); a backend
// only has to translate/execute code against it. Imported functions, JNI stubs,
// kernel helpers and the end-of-call marker are `svc #imm24` instructions
// ("traps") delivered to CpuHost::onTrap.
//
// Backends: dynarmic (fast JIT; default) and Unicorn (QEMU TCG; slower, has
// debugging hooks). Selected at build time with NIN_CPU.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace nin {

enum Reg : int { kR0 = 0, kR1, kR2, kR3, kR4, kR5, kR6, kR7, kR8, kR9, kR10, kR11, kR12, kSP, kLR, kPC, kCPSR };

// Trap numbers (svc immediates). Low 16 bits carry an index within the group.
constexpr uint32_t kTrapImport = 0x010000;
constexpr uint32_t kTrapTableBase = 0x020000;  // JNI tables etc. (0x02.., 0x03.., ...)
constexpr uint32_t kTrapTableEnd = 0x7e0000;
constexpr uint32_t kTrapStop = 0x7fffff;

constexpr uint32_t svcInstruction(uint32_t imm24) { return 0xef000000u | (imm24 & 0xffffffu); }
constexpr uint32_t kBxLr = 0xe12fff1eu;

class CpuHost {
 public:
  virtual ~CpuHost() = default;
  virtual void onTrap(uint32_t number) = 0;
  // Execution failed (invalid memory access, unsupported instruction...).
  virtual void onFault(const std::string& what) = 0;
  virtual std::string describe(uint32_t address) const = 0;
};

class Cpu {
 public:
  virtual ~Cpu() = default;
  virtual const char* name() const = 0;
  // Make guest [g, g+size) accessible; host memory at base+g is already RW.
  virtual void mapRegion(uint32_t g, uint32_t size) = 0;
  virtual uint32_t reg(int r) = 0;
  virtual void setReg(int r, uint32_t v) = 0;
  virtual uint64_t dreg(int n) = 0;  // VFP D0-D31
  virtual void setDreg(int n, uint64_t v) = 0;
  // Nested calls (a trap handler calling back into guest code) get a fresh
  // register context; leave() restores the caller's.
  virtual void enter() = 0;
  virtual void leave() = 0;
  // Run from pc until stop() (called for the stop trap). False if a fault
  // was reported through CpuHost::onFault.
  virtual bool run(uint32_t pc) = 0;
  virtual void stop() = 0;
  virtual void invalidateCode(uint32_t, uint32_t) {}
};

// stop_address holds the stop trap; calls return to it.
std::unique_ptr<Cpu> makeCpu(CpuHost* host, uint8_t* base, uint32_t stop_address);

}  // namespace nin

#include "engine_patch.h"

#include <array>
#include <cstring>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#if defined(__aarch64__) && defined(__linux__)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif

#include "guest.h"
#include "log.h"

namespace nin {
namespace {

constexpr uint32_t kRound[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

using Sha256State = std::array<uint32_t, 8>;

void compressPortable(Sha256State& h, const uint8_t* data, size_t blocks) {
  auto rotr = [](uint32_t v, int n) { return (v >> n) | (v << (32 - n)); };
  for (; blocks > 0; blocks--, data += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
      w[i] = uint32_t(data[4 * i]) << 24 | uint32_t(data[4 * i + 1]) << 16 | uint32_t(data[4 * i + 2]) << 8 |
             data[4 * i + 3];
    for (int i = 16; i < 64; i++) {
      uint32_t a = w[i - 15], b = w[i - 2];
      w[i] = w[i - 16] + (rotr(a, 7) ^ rotr(a, 18) ^ (a >> 3)) + w[i - 7] +
             (rotr(b, 17) ^ rotr(b, 19) ^ (b >> 10));
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kRound[i] + w[i];
      uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
}

#if defined(__aarch64__)
// The ARMv8 SHA-256 instructions, present on practically every arm64 phone.
__attribute__((target("sha2"))) void compressArm(Sha256State& h, const uint8_t* data, size_t blocks) {
  uint32x4_t s0 = vld1q_u32(&h[0]), s1 = vld1q_u32(&h[4]);
  for (; blocks > 0; blocks--, data += 64) {
    const uint32x4_t save0 = s0, save1 = s1;
    uint32x4_t msg[4];
    for (int i = 0; i < 4; i++) msg[i] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(data + 16 * i)));
    for (int i = 0; i < 16; i++) {
      const uint32x4_t wk = vaddq_u32(msg[i % 4], vld1q_u32(&kRound[4 * i]));
      const uint32x4_t prev = s0;
      s0 = vsha256hq_u32(s0, s1, wk);
      s1 = vsha256h2q_u32(s1, prev, wk);
      if (i < 12)
        msg[i % 4] = vsha256su1q_u32(vsha256su0q_u32(msg[i % 4], msg[(i + 1) % 4]), msg[(i + 2) % 4],
                                     msg[(i + 3) % 4]);
    }
    s0 = vaddq_u32(s0, save0);
    s1 = vaddq_u32(s1, save1);
  }
  vst1q_u32(&h[0], s0);
  vst1q_u32(&h[4], s1);
}
#endif

// SHA-256 of the entire original ELF, not just its patched instructions. Kept
// local so the boot check needs no Android crypto library or JNI.
Sha256State sha256(const std::vector<uint8_t>& image) {
  auto compress = compressPortable;
#if defined(__aarch64__) && defined(__linux__)
  if (getauxval(AT_HWCAP) & HWCAP_SHA2) compress = compressArm;
#endif
  Sha256State h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const size_t full = image.size() / 64;
  compress(h, image.data(), full);
  // Padding: 0x80, zeros, then the length in bits, big-endian, in one or two blocks.
  uint8_t tail[128] = {};
  const size_t rest = image.size() % 64;
  std::memcpy(tail, image.data() + full * 64, rest);
  tail[rest] = 0x80;
  const size_t tailBlocks = rest + 9 > 64 ? 2 : 1;
  const uint64_t bits = uint64_t(image.size()) * 8;
  for (int i = 0; i < 8; i++) tail[tailBlocks * 64 - 1 - i] = uint8_t(bits >> (8 * i));
  compress(h, tail, tailBlocks);
  return h;
}

}  // namespace

bool installEnginePatches(Guest& g, const std::vector<uint8_t>& image,
                          const EnginePatch* patches, size_t count, const char* label) {
  constexpr std::array<uint32_t, 8> expected = {0x813e4ce3, 0xb7fb3286, 0x92000ca8, 0x7b7c99ff,
                                               0xb1a9b490, 0xd1daeea5, 0x0b451622, 0xd1626995};
  // Computed once per boot: every patch set checks the same image.
  if (g.engineImageVerified < 0) g.engineImageVerified = sha256(image) == expected;
  if (!g.engineImageVerified) {
    NIN_LOGW("%s refused: engine SHA-256 mismatch (no patches applied)", label);
    return false;
  }
  for (size_t i = 0; i < count; i++) {
    const auto& p = patches[i];
    if (uint64_t(kImageBase) + p.offset + 4 > g.imageEnd() || g.r32(kImageBase + p.offset) != p.before) {
      NIN_LOGW("%s refused: instruction mismatch at ELF %08x (no patches applied)", label, p.offset);
      return false;
    }
  }
  for (size_t i = 0; i < count; i++)
    if (patches[i].before != patches[i].after)
      g.w32(kImageBase + patches[i].offset, patches[i].after);
  return true;
}

}  // namespace nin

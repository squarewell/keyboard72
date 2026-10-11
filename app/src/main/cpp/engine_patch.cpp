#include "engine_patch.h"

#include <array>

#include "guest.h"
#include "log.h"

namespace nin {
namespace {

// SHA-256 of the entire original ELF, not just its patched instructions. Kept
// local so the optional boot check needs no Android crypto library or JNI.
std::array<uint32_t, 8> sha256(const std::vector<uint8_t>& image) {
  constexpr uint32_t k[] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  std::array<uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  auto rotr = [](uint32_t v, int n) { return (v >> n) | (v << (32 - n)); };
  const size_t padded = ((image.size() + 9 + 63) / 64) * 64;
  const uint64_t bits = uint64_t(image.size()) * 8;
  for (size_t offset = 0; offset < padded; offset += 64) {
    uint32_t w[64] = {};
    for (size_t i = 0; i < 64; i++) {
      const size_t p = offset + i;
      uint8_t b = p < image.size() ? image[p] : p == image.size() ? 0x80 : 0;
      if (p >= padded - 8) b = uint8_t(bits >> ((padded - 1 - p) * 8));
      w[i / 4] |= uint32_t(b) << ((3 - i % 4) * 8);
    }
    for (int i = 16; i < 64; i++) {
      uint32_t a = w[i - 15], b = w[i - 2];
      w[i] = w[i - 16] + (rotr(a, 7) ^ rotr(a, 18) ^ (a >> 3)) + w[i - 7] +
             (rotr(b, 17) ^ rotr(b, 19) ^ (b >> 10));
    }
    auto v = h;
    for (int i = 0; i < 64; i++) {
      uint32_t t1 = v[7] + (rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25)) +
                    ((v[4] & v[5]) ^ (~v[4] & v[6])) + k[i] + w[i];
      uint32_t t2 = (rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22)) +
                    ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
      for (int j = 7; j > 0; j--) v[j] = v[j - 1];
      v[4] += t1;
      v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) h[i] += v[i];
  }
  return h;
}

}  // namespace

bool installEnginePatches(Guest& g, const std::vector<uint8_t>& image,
                          const EnginePatch* patches, size_t count, const char* label) {
  constexpr std::array<uint32_t, 8> expected = {0x813e4ce3, 0xb7fb3286, 0x92000ca8, 0x7b7c99ff,
                                               0xb1a9b490, 0xd1daeea5, 0x0b451622, 0xd1626995};
  if (sha256(image) != expected) {
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

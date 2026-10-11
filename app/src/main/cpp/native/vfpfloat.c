/* Hardware (VFP) replacements for the engine's software floating point.
 *
 * The engine was built for the armeabi soft-float ABI, so every float/double
 * operation calls a libgcc routine (__aeabi_fadd, __divsf3, __cmpsf2, ...)
 * that takes 20-60 integer instructions. The shim rewrites the first
 * instruction of each routine into a branch to the matching function here.
 * These keep the soft-float calling convention (arguments and results in core
 * registers, -mfloat-abi=softfp) but compute with single VFP instructions.
 *
 * The results are bit for bit what the engine's own libgcc returns (checked by
 * app/src/main/cpp/desktop/float_test.cpp), except NaN payloads. The CPU's FPSCR is 0:
 * round to nearest, no flush-to-zero, no default NaN. Special cases follow
 * the libgcc ARM sources (ieee754-sf.S / ieee754-df.S): unordered comparison
 * results, the flag-setting __aeabi_cf* helpers (N/Z/C/V exactly as libgcc
 * leaves them, and its operand swap in the reversed variants), and saturating
 * conversions with NaN -> 0.
 *
 * That libgcc is not always correctly rounded in addition and subtraction:
 * with a denormal operand or result (float and double), and in double
 * subtraction whose operand exponents differ by exactly 33 (half of the cases
 * where the result's exponent drops are one ulp off). Those inputs go to the
 * original code through the trampolines below, so the engine computes exactly
 * what it computed on a phone. Everything else matches IEEE exactly.
 */
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long i64;

/* The engine's __aeabi_fadd / __aeabi_dadd. Guest::installHardFloat fills each
 * with the routine's first instruction and a branch to its second one. */
__attribute__((naked)) float fast_soft_fadd(float a, float b)
{
    __asm__ volatile(".word 0xe7f000f0\n\t.word 0xe7f000f0\n\t");  /* udf until filled */
}

__attribute__((naked)) double fast_soft_dadd(double a, double b)
{
    __asm__ volatile(".word 0xe7f000f0\n\t.word 0xe7f000f0\n\t");
}

static inline u32 fbits(float f) { u32 u; __builtin_memcpy(&u, &f, 4); return u; }
static inline float ffrom(u32 u) { float f; __builtin_memcpy(&f, &u, 4); return f; }
static inline u64 dbits(double d) { u64 u; __builtin_memcpy(&u, &d, 8); return u; }
static inline double dfrom(u64 u) { double d; __builtin_memcpy(&d, &u, 8); return d; }

/* Nonzero with a zero exponent field. */
static inline int denormal_f(u32 x) { return (u32)(x << 1) - 1 < 0x00ffffffu; }
static inline int denormal_d(u64 x) { return (u64)(x << 1) - 1 < 0x001fffffffffffffull; }

static inline float add_f(u32 x, u32 y)
{
    float r = ffrom(x) + ffrom(y);
    if (__builtin_expect(denormal_f(x) | denormal_f(y) | denormal_f(fbits(r)), 0))
        return fast_soft_fadd(ffrom(x), ffrom(y));
    return r;
}

static __attribute__((noinline)) double add_d_rare(u64 x, u64 y)
{
    int gap = (int)((x >> 52) & 0x7ff) - (int)((y >> 52) & 0x7ff);
    if ((((x ^ y) >> 63) && (gap == 33 || gap == -33)) || denormal_d(x) || denormal_d(y))
        return fast_soft_dadd(dfrom(x), dfrom(y));
    double r = dfrom(x) + dfrom(y);
    return denormal_d(dbits(r)) ? fast_soft_dadd(dfrom(x), dfrom(y)) : r;
}

/* Screens on the exponent fields only: zero/denormal operands or result, or a
 * gap of 33, take the exact check above. */
static inline double add_d(u64 x, u64 y)
{
    u32 ex = (u32)(x >> 52) & 0x7ff, ey = (u32)(y >> 52) & 0x7ff, gap = ex - ey + 33;
    if (__builtin_expect(ex == 0 || ey == 0 || gap == 0 || gap == 66, 0))
        return add_d_rare(x, y);
    double r = dfrom(x) + dfrom(y);
    if (__builtin_expect(((u32)(dbits(r) >> 52) & 0x7ff) == 0, 0))
        return add_d_rare(x, y);
    return r;
}

/* --- single precision --------------------------------------------------- */

/* libgcc's fsub and frsub flip a sign bit and continue in fadd. */
float fast_fadd(float a, float b) { return add_f(fbits(a), fbits(b)); }
float fast_fsub(float a, float b) { return add_f(fbits(a), fbits(b) ^ 0x80000000u); }
float fast_frsub(float a, float b) { return add_f(fbits(a) ^ 0x80000000u, fbits(b)); }
float fast_fmul(float a, float b) { return a * b; }
float fast_fdiv(float a, float b) { return a / b; }

int fast_fcmpeq(float a, float b) { return a == b; }
int fast_fcmplt(float a, float b) { return a < b; }
int fast_fcmple(float a, float b) { return a <= b; }
int fast_fcmpge(float a, float b) { return a >= b; }
int fast_fcmpgt(float a, float b) { return a > b; }
int fast_fcmpun(float a, float b) { return __builtin_isunordered(a, b); }

/* __cmpsf2/__eqsf2/__nesf2 and __lesf2/__ltsf2: unordered -> 1 */
int fast_cmpsf2(float a, float b) { return a < b ? -1 : a == b ? 0 : 1; }
/* __gesf2/__gtsf2: unordered -> -1 */
int fast_gesf2(float a, float b) { return a > b ? 1 : a == b ? 0 : -1; }

/* __aeabi_cfcmple/__aeabi_cfcmpeq: flags as for "cmp a, b"; r0-r3 preserved.
 * libgcc: less N=1 Z=0 C=0 V=0, equal Z=1 C=1, greater and unordered C=1
 * (N=0 Z=0 V=0). vcmp matches except that unordered also sets V. */
__attribute__((naked)) void fast_cfcmple(void)
{
    __asm__ volatile("vmov s0, r0\n\t"
                     "vmov s1, r1\n\t"
                     "vcmp.f32 s0, s1\n\t"
                     "vmrs APSR_nzcv, fpscr\n\t"
                     "bxvc lr\n\t"
                     "mov ip, #1\n\t"
                     "cmp ip, #0\n\t"
                     "bx lr\n\t");
}

/* __aeabi_cfrcmple: libgcc swaps r0/r1 and leaves them swapped on return. */
__attribute__((naked)) void fast_cfrcmple(void)
{
    __asm__ volatile("mov ip, r0\n\t"
                     "mov r0, r1\n\t"
                     "mov r1, ip\n\t"
                     "b fast_cfcmple\n\t");
}

float fast_i2f(int x) { return (float)x; }
float fast_ui2f(u32 x) { return (float)x; }
int fast_f2iz(float x) { return (int)x; }   /* vcvt: saturates, NaN -> 0 */
u32 fast_f2uiz(float x) { return (u32)x; }  /* vcvt: negative -> 0, NaN -> 0 */

/* --- double precision --------------------------------------------------- */

double fast_dadd(double a, double b) { return add_d(dbits(a), dbits(b)); }
double fast_dsub(double a, double b) { return add_d(dbits(a), dbits(b) ^ (1ull << 63)); }
double fast_drsub(double a, double b) { return add_d(dbits(a) ^ (1ull << 63), dbits(b)); }
double fast_dmul(double a, double b) { return a * b; }
double fast_ddiv(double a, double b) { return a / b; }

int fast_dcmpeq(double a, double b) { return a == b; }
int fast_dcmplt(double a, double b) { return a < b; }
int fast_dcmple(double a, double b) { return a <= b; }
int fast_dcmpge(double a, double b) { return a >= b; }
int fast_dcmpgt(double a, double b) { return a > b; }
int fast_dcmpun(double a, double b) { return __builtin_isunordered(a, b); }

int fast_cmpdf2(double a, double b) { return a < b ? -1 : a == b ? 0 : 1; }
int fast_gedf2(double a, double b) { return a > b ? 1 : a == b ? 0 : -1; }

__attribute__((naked)) void fast_cdcmple(void)
{
    __asm__ volatile("vmov d0, r0, r1\n\t"
                     "vmov d1, r2, r3\n\t"
                     "vcmp.f64 d0, d1\n\t"
                     "vmrs APSR_nzcv, fpscr\n\t"
                     "bxvc lr\n\t"
                     "mov ip, #1\n\t"
                     "cmp ip, #0\n\t"
                     "bx lr\n\t");
}

/* __aeabi_cdrcmple: libgcc swaps r0:r1 with r2:r3 and leaves them swapped. */
__attribute__((naked)) void fast_cdrcmple(void)
{
    __asm__ volatile("mov ip, r0\n\t"
                     "mov r0, r2\n\t"
                     "mov r2, ip\n\t"
                     "mov ip, r1\n\t"
                     "mov r1, r3\n\t"
                     "mov r3, ip\n\t"
                     "b fast_cdcmple\n\t");
}

double fast_i2d(int x) { return (double)x; }
double fast_ui2d(u32 x) { return (double)x; }
int fast_d2iz(double x) { return (int)x; }
u32 fast_d2uiz(double x) { return (u32)x; }
double fast_f2d(float x) { return (double)x; }
float fast_d2f(double x) { return (float)x; }

/* --- 64-bit integers ------------------------------------------------------
 * VFP has no 64-bit integer conversions. hi * 2^32 and lo are both exact
 * doubles, so one IEEE addition gives the correctly rounded result. */

double fast_l2d(i64 x) { return (double)(int)(x >> 32) * 4294967296.0 + (double)(u32)x; }
double fast_ul2d(u64 x) { return (double)(u32)(x >> 32) * 4294967296.0 + (double)(u32)x; }

/* To float: exact through double below 2^53. Above, shift to 53 bits and fold
 * the lost bits into the lowest bit (round to odd) so that the final rounding
 * to 24 bits is still correct. */
static float u64_to_float(u64 m)
{
    u32 scale = 1;
    if (m >> 53) {
        int s = 11 - __builtin_clzll(m);
        u64 lost = m & ((1ull << s) - 1);
        m = (m >> s) | (lost != 0);
        scale = 1u << s;
    }
    double d = (double)(u32)(m >> 32) * 4294967296.0 + (double)(u32)m;
    return (float)(d * (double)scale);
}

float fast_ul2f(u64 x) { return u64_to_float(x); }
float fast_l2f(i64 x) { return x < 0 ? -u64_to_float(-(u64)x) : u64_to_float((u64)x); }

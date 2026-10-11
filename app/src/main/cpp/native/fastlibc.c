/* Guest-side (ARM) implementations of hot, pure libc imports.
 *
 * Linked at a fixed guest address by build_native.py and called directly by
 * the engine through its GOT, so these calls never leave the emulator.
 * Semantics follow bionic: memcmp/strcmp return the difference of the first
 * differing bytes as unsigned char, not just -1/0/1.
 */
typedef unsigned int u32;
typedef unsigned char u8;

#define CLOCK_WORDS ((volatile u32 *)0x0201f000) /* {tv_sec, tv_usec}, kept by the host */

void *fast_memcpy(void *dst, const void *src, u32 n)
{
    u8 *d = dst;
    const u8 *s = src;
    if ((((u32)d | (u32)s) & 3) == 0) {
        while (n >= 16) {
            u32 a = ((const u32 *)s)[0], b = ((const u32 *)s)[1];
            u32 c = ((const u32 *)s)[2], e = ((const u32 *)s)[3];
            ((u32 *)d)[0] = a; ((u32 *)d)[1] = b; ((u32 *)d)[2] = c; ((u32 *)d)[3] = e;
            d += 16; s += 16; n -= 16;
        }
        while (n >= 4) {
            *(u32 *)d = *(const u32 *)s;
            d += 4; s += 4; n -= 4;
        }
    }
    while (n--)
        *d++ = *s++;
    return dst;
}

void *fast_memmove(void *dst, const void *src, u32 n)
{
    u8 *d = dst;
    const u8 *s = src;
    if (d <= s || d >= s + n)
        return fast_memcpy(dst, src, n);
    d += n;
    s += n;
    while (n--)
        *--d = *--s;
    return dst;
}

void *fast_memset(void *dst, int c, u32 n)
{
    u8 *d = dst;
    u32 v = (u8)c;
    v |= v << 8;
    v |= v << 16;
    while (n && ((u32)d & 3)) {
        *d++ = (u8)c;
        n--;
    }
    while (n >= 4) {
        *(u32 *)d = v;
        d += 4;
        n -= 4;
    }
    while (n--)
        *d++ = (u8)c;
    return dst;
}

void fast_aeabi_memset(void *dst, u32 n, int c) /* note (dest, n, c) order */
{
    fast_memset(dst, c, n);
}

u32 fast_strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (u32)(p - s);
}

int fast_strcmp(const char *a, const char *b)
{
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    while (*x && *x == *y) {
        x++;
        y++;
    }
    return (int)*x - (int)*y;
}

int fast_strncmp(const char *a, const char *b, u32 n)
{
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    for (; n; n--, x++, y++) {
        if (*x != *y)
            return (int)*x - (int)*y;
        if (!*x)
            return 0;
    }
    return 0;
}

int fast_memcmp(const void *a, const void *b, u32 n)
{
    const u8 *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y)
            return (int)*x - (int)*y;
    return 0;
}

int fast_gettimeofday(u32 *tv, void *tz)
{
    if (tv) {
        tv[0] = CLOCK_WORDS[0];
        tv[1] = CLOCK_WORDS[1];
    }
    return 0;
}

typedef void (*once_fn)(void);

int fast_pthread_once(volatile int *control, once_fn fn)
{
    if (*control == 0) {
        *control = 1;
        fn();
    }
    return 0;
}

int fast_return0(void)
{
    return 0;
}

int fast_tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int fast_toupper(int c)
{
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

/* ---------------------------------------------------------------------------
 * Allocator: same algorithm as the harness's Python Heap, so addresses are
 * identical. Blocks are 16-byte aligned with a 16-byte gap before each user
 * pointer; the size lives at p-8 and a liveness tag at p-4. Freed blocks go
 * on per-size LIFO lists (exact size match, including large blocks).
 */
#define HEAP_STATE ((volatile u32 *)0x0201e000)
/* HEAP_STATE[0] = top, [1] = end, [2] = large-list head, [3] = reserved,
 * [4 + size/16] = small free-list heads for sizes 16..4096 */
#define LIVE_TAG 0xa110c8edu
#define FREE_TAG 0x0f4ee000u
#define SMALL_MAX 4096u

static void fault(void)
{
    *(volatile u32 *)0 = 0; /* unmapped: stops emulation with an error */
}

void *fast_malloc(u32 n)
{
    u32 size = n < 16 ? 16 : (n + 15) & ~15u;
    u32 *p = 0;
    if (size <= SMALL_MAX) {
        volatile u32 *head = &HEAP_STATE[4 + size / 16];
        if (*head) {
            p = (u32 *)*head;
            *head = p[0];
        }
    } else {
        volatile u32 *link = &HEAP_STATE[2];
        while (*link) {
            u32 *q = (u32 *)*link;
            if (q[-2] == size) {
                *link = q[0];
                p = q;
                break;
            }
            link = (volatile u32 *)&q[0];
        }
    }
    if (!p) {
        u32 address = HEAP_STATE[0] + 16;
        if (address + size > HEAP_STATE[1] || address + size < address) {
            fault();
            return 0;
        }
        HEAP_STATE[0] = address + size;
        p = (u32 *)address;
    }
    p[-2] = size;
    p[-1] = LIVE_TAG;
    return p;
}

void fast_free(void *ptr)
{
    u32 *p = ptr;
    if (!p)
        return;
    if (p[-1] != LIVE_TAG) {
        fault();
        return;
    }
    p[-1] = FREE_TAG;
    u32 size = p[-2];
    if (size <= SMALL_MAX) {
        volatile u32 *head = &HEAP_STATE[4 + size / 16];
        p[0] = *head;
        *head = (u32)p;
    } else {
        /* LIFO per exact size: insert at list head; lookup takes first match. */
        p[0] = HEAP_STATE[2];
        HEAP_STATE[2] = (u32)p;
    }
}

void *fast_calloc(u32 count, u32 size)
{
    u32 n = count * size;
    void *p = fast_malloc(n);
    fast_memset(p, 0, n);
    return p;
}

void *fast_realloc(void *ptr, u32 n)
{
    u32 *p = ptr;
    if (!p)
        return fast_malloc(n);
    if (n == 0) {
        fast_free(p);
        return 0;
    }
    if (p[-1] != LIVE_TAG) {
        fault();
        return 0;
    }
    u32 old = p[-2];
    if (n <= old)
        return p;
    void *q = fast_malloc(n);
    fast_memcpy(q, p, old);
    fast_free(p);
    return q;
}

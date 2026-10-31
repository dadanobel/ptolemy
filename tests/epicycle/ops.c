// Differential test: the same C code natively (ARM64) and through the engine (PPC). No undefined behaviour.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static uint32_t s = 12345;
static uint32_t rnd(void) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
struct bf { unsigned a : 3, b : 7, c : 13; signed d : 5; };
typedef uint32_t (*fn)(uint32_t, uint32_t);
static uint32_t f0(uint32_t a, uint32_t b) { return a + b; }
static uint32_t f1(uint32_t a, uint32_t b) { return a ^ (b >> 3); }
static uint32_t f2(uint32_t a, uint32_t b) { return a * 3 - b; }
static fn tab[3] = {f0, f1, f2};
static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
static int sw(uint32_t x) { switch (x & 15) { case 0: return 7; case 1: return x >> 3; case 2: return -3; case 3: return x * 5;
  case 5: return x ^ 0x55; case 8: return 100; case 9: return (int)x >> 1; case 13: return 2; default: return (int)(x % 7); } }
int main(void) {
  uint64_t h = 1469598103934665603ull;
#define MIX(v) (h = (h ^ (uint64_t)(v)) * 1099511628211ull)
  for (int i = 0; i < 20000; i++) {
    uint32_t a = rnd(), b = rnd(), n = b & 31; int32_t sa = (int32_t)a, sb = (int32_t)b | 1;
    uint64_t A = (uint64_t)a << 32 | b, B = (uint64_t)rnd() << 32 | rnd();
    MIX(a + b); MIX(a - b); MIX(a * b); MIX(a / (b | 1)); MIX(a % (b | 1)); MIX(sa / sb); MIX(sa % sb);
    MIX(a << n); MIX(a >> n); MIX(sa >> n); MIX((a << n) | (a >> ((32 - n) & 31))); MIX(__builtin_clz(a | 1));
    MIX(A + B); MIX(A - B); MIX(A * B); MIX(A / (B | 1)); MIX((int64_t)A >> (b & 63)); MIX(A << (a & 63)); MIX(A >> (a & 63));
    MIX((int64_t)sa * sb); MIX((uint64_t)a * b); MIX(a < b); MIX(sa < sb); MIX((int8_t)a); MIX((int16_t)b); MIX((uint16_t)a);
    struct bf x; x.a = a; x.c = a >> 7; x.b = b; x.d = (int)b; MIX(x.a + x.b + x.c + x.d);
    MIX(tab[a % 3](a, b)); MIX(sw(a)); MIX(a > b ? a - b : b - a); MIX(__builtin_bswap32(a)); MIX(__builtin_popcount(a));
    double d = (double)sa / (double)sb, e = (double)a * 1e-3; MIX((int64_t)(d * 1e6)); MIX((int32_t)e); MIX(e > d); MIX((uint32_t)(float)b);
    unsigned char buf[16]; for (int k = 0; k < 8; k++) { buf[k] = A >> (56 - 8 * k); buf[8 + k] = B >> (56 - 8 * k); }
    const unsigned char *q = buf + (a & 7); uint32_t u = (uint32_t)q[0] << 24 | q[1] << 16 | q[2] << 8 | q[3]; MIX(u);
    uint16_t hw; memcpy(&hw, buf + (b & 7), 2); MIX(hw == (uint16_t)(buf[b & 7] | buf[(b & 7) + 1] << 8) || hw == (uint16_t)(buf[b & 7] << 8 | buf[(b & 7) + 1]));
  }
  printf("hash %016llx fib %d\n", (unsigned long long)h, fib(20));
  return 0;
}

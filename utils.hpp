#define LIKEKLY(x) __builtin_expect((x), 1)
#define UNLIKELY(x) __builtin_expect((x), 0)
#define NOINLINE __attribute__((noinline))
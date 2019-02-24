#define LIKEKLY(x) __builtin_expect((x), 1)
#define UNLIKELY(x) __builtin_expect((x), 0)

#define REAL_NOINLINE __attribute__((noinline))

#if 0
#define NOINLINE REAL_NOINLINE
#else
#define NOINLINE
#endif
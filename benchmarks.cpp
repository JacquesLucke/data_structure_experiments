#include "hash_set.hpp"
#include "hashing.hpp"
#include <benchmark/benchmark.h>

uint32_t my_hash(const int &v) REAL_NOINLINE {
    uint32_t value = v;
    value = ~value + (value << 15);
    value = value ^ (value >> 12);
    value = value + (value << 2);
    value = value ^ (value >> 4);
    value = value * 2057;
    value = value ^ (value >> 16);
    return value;
}

using IntSet = HashSet<int, HashBits32>;

static void BM_HashSet_InsertNew(benchmark::State &state) {
    IntSet set;
    for (auto _ : state) {
        state.PauseTiming();
        set = IntSet();
        state.ResumeTiming();
        for (int i = 0; i < state.range(0); i++) {
            set.insert_new(i);
        }
    }
    state.SetItemsProcessed(state.iterations() *
                            state.range(0));
}

BENCHMARK(BM_HashSet_InsertNew)->Range(8, 8 << 25);

BENCHMARK_MAIN();
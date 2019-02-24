#include "hash_set.hpp"
#include "hashing.hpp"
#include <benchmark/benchmark.h>

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

BENCHMARK(BM_HashSet_InsertNew)->Range(8, 8 << 20);

BENCHMARK_MAIN();
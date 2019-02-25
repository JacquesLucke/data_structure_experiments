#include "hash_set.hpp"
#include "hashing.hpp"
#include <benchmark/benchmark.h>

using IntSet = HashSet<int, HashBits32>;

static void BM_HashSet_Insert(benchmark::State &state) {
    for (auto _ : state) {
        state.PauseTiming();
        IntSet set;
        state.ResumeTiming();
        for (int i = 0; i < state.range(0); i++) {
            set.insert(i);
        }
    }
    state.SetItemsProcessed(state.iterations() *
                            state.range(0));
}

static void BM_HashSet_InsertNew(benchmark::State &state) {
    for (auto _ : state) {
        state.PauseTiming();
        IntSet set;
        state.ResumeTiming();
        for (int i = 0; i < state.range(0); i++) {
            set.insert_new(i);
        }
    }
    state.SetItemsProcessed(state.iterations() *
                            state.range(0));
}

static void
BM_HashSet_InsertManyNew(benchmark::State &state) {
    for (auto _ : state) {
        state.PauseTiming();
        IntSet set = IntSet();
        state.ResumeTiming();
        std::vector<int> values(state.range(0));
        for (int i = 0; i < state.range(0); i++) {
            values[i] = i;
        }
        set.insert_many_new(values);
    }
    state.SetItemsProcessed(state.iterations() *
                            state.range(0));
}

BENCHMARK(BM_HashSet_Insert)->Range(8, 8 << 20);
BENCHMARK(BM_HashSet_InsertNew)->Range(8, 8 << 20);
BENCHMARK(BM_HashSet_InsertManyNew)->Range(8, 8 << 20);

BENCHMARK_MAIN();
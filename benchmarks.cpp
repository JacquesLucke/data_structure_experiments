#include "hash_set.hpp"
#include "hashing.hpp"
#include <benchmark/benchmark.h>
#include <unordered_set>

using IntSet = HashSet<int, HashBits32>;

static void BM_HashSet_Insert(benchmark::State &state) {
    IntSet set;
    for (auto _ : state) {
        state.PauseTiming();
        set = {};
        state.ResumeTiming();
        for (int i = 0; i < state.range(0); i++) {
            set.insert(i);
        }
    }
    state.SetItemsProcessed(state.iterations() *
                            state.range(0));
}

static void
BM_UnorderedSet_Insert(benchmark::State &state) {
    std::unordered_set<int> set;
    for (auto _ : state) {
        state.PauseTiming();
        set = {};
        state.ResumeTiming();
        for (int i = 0; i < state.range(0); i++) {
            set.insert(i);
        }
    }
    state.SetItemsProcessed(state.iterations() *
                            state.range(0));
}

static void BM_HashSet_InsertNew(benchmark::State &state) {
    IntSet set;
    for (auto _ : state) {
        state.PauseTiming();
        set = {};
        state.ResumeTiming();
        for (int i = 0; i < state.range(0); i++) {
            set.insert_new(i);
        }
    }
    state.SetItemsProcessed(state.iterations() *
                            state.range(0));
}

static void
BM_HashSet_BuildFromVector(benchmark::State &state) {
    IntSet set;
    for (auto _ : state) {
        state.PauseTiming();
        set = {};
        state.ResumeTiming();
        std::vector<int> values(state.range(0));
        for (int i = 0; i < state.range(0); i++) {
            values[i] = i;
        }
        set = IntSet(values);
    }
    state.SetItemsProcessed(state.iterations() *
                            state.range(0));
}

BENCHMARK(BM_HashSet_Insert)->Range(8, 8 << 20);
BENCHMARK(BM_UnorderedSet_Insert)->Range(8, 8 << 20);
BENCHMARK(BM_HashSet_InsertNew)->Range(8, 8 << 20);
BENCHMARK(BM_HashSet_BuildFromVector)->Range(8, 8 << 20);

BENCHMARK_MAIN();
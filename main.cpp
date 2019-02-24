#include "hash_set.hpp"
#include "hashing.hpp"
#include "timeit.hpp"
#include <iostream>
#include <stdint.h>

using IntSet = HashSet<int, HashBits32>;
using StringSet = HashSet<std::string, HashString>;

int main() {
    IntSet set;
    for (int i = 0; i < 100; i++) {
        set.insert(i);
    }

    std::cout << "Values:" << std::endl;
    for (int v : set) {
        std::cout << v << " ";
    }

    std::cout << std::endl;
}

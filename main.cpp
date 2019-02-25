#include "hash_set.hpp"
#include "hashing.hpp"
#include "timeit.hpp"
#include <iostream>
#include <stdint.h>

using IntSet = HashSet<int, HashBits32>;
using StringSet = HashSet<std::string, HashString>;

int main() {
    IntSet set;
    std::vector<int> values = {4, 6, 8};
    set.insert_many_new(values);

    std::cout << "Values:" << std::endl;
    for (int v : set) {
        std::cout << v << " ";
    }

    std::cout << std::endl;
}

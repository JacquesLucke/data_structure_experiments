#include "hash_set.hpp"
#include "hashing.hpp"
#include "timeit.hpp"
#include <iostream>
#include <stdint.h>

using IntSet = HashSet<int, HashBits32>;
using StringSet = HashSet<std::string, HashString>;

int main() {
    StringSet set = {"Where", "Who", "When"};

    std::cout << set.size() << " Values:" << std::endl;
    for (auto v : set) {
        std::cout << v << " ";
    }

    set.print_state();

    std::cout << std::endl;
}

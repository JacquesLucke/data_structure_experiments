#include "hash_set.hpp"
#include "hashing.hpp"
#include "timeit.hpp"
#include <iostream>
#include <stdint.h>

uint32_t my_hash(const int &v) NOINLINE {
    uint32_t value = v;
    value = ~value + (value << 15);
    value = value ^ (value >> 12);
    value = value + (value << 2);
    value = value ^ (value >> 4);
    value = value * 2057;
    value = value ^ (value >> 16);
    return value;
}

int main() {
    HashSet<int, HashBits32> set;

    set.insert_new(5);
    set.insert_new(7);
    set.insert_new(2);
    set.insert_new(3);

    for (int i = 100; i < 10000; i++) {
        if (i % 4 == 0) {
            set.insert_new(i);
        }
    }

    set.remove(500);

    for (uint i = 490; i < 510; i++) {
        bool contained = set.contains(i);
        std::cout << i << ": " << contained << std::endl;
    }
}

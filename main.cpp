#include "hash_set.hpp"
#include "timeit.hpp"
#include <iostream>
#include <stdint.h>

uint32_t my_hash(const int &v) {
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
    HashSet<int, my_hash> set;

    {
        TIMEIT("test");
        for (int i = 0; i < 10000; i++) {
            set.insert_new(i);
        }
    }

    std::cout << "Size: " << (int)set.size() << std::endl;

    for (int i = 0; i < 100; i++) {
        // set.contains(i);
        // std::cout << "Contains " << i << ": " <<  << std::endl;
    }
}

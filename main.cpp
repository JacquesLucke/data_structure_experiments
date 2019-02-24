#include "hash_set.hpp"
#include "hashing.hpp"
#include "timeit.hpp"
#include <iostream>
#include <stdint.h>

using IntSet = HashSet<int, HashBits32>;
using StringSet = HashSet<std::string, HashString>;

int main() {
    StringSet set;
    set.insert("Hello World");
}

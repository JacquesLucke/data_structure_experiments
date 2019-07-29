
#include <iostream>
#include <random>
#include <stdint.h>

typedef unsigned int uint;

#define PERTURB_SHIFT 5

void handle(uint slot) {}

void iter_slots(uint32_t hash, uint32_t group_mask,
                uint32_t offset_mask) {
    uint32_t perturb = hash;
    do {
        uint group = hash & group_mask;
        uint offset_in_group = hash & offset_mask;
        uint initial_offset = offset_in_group;
        do {
            uint slot = group | offset_in_group;
            std::cout << "Group: " << group
                      << "\tSlot: " << slot << '\n';
            if (slot == 0) return;
            offset_in_group =
                (offset_in_group + 1) & offset_mask;
        } while (offset_in_group != initial_offset);
        perturb >>= PERTURB_SHIFT;
        hash = hash * 5 + 1 + perturb;
    } while (true);
}

int main(int argc, char const *argv[]) {
    int size = 1 << 10;
    uint32_t offset_mask = 3;
    uint32_t group_mask = (size - 1) & ~offset_mask;

    srand(time(NULL));
    uint32_t hash = rand();
    iter_slots(hash, group_mask, offset_mask);
    return 0;
}

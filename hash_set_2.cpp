
#include <iostream>
#include <random>
#include <stdint.h>

typedef unsigned int uint;

#define PERTURB_SHIFT 5
#define OFFSET_MASK 3

// clang-format off

#define ITER_SLOTS_BEGIN(HASH, GROUP_MASK, R_SLOT)         \
    uint32_t hash_ = HASH;                                 \
    uint32_t perturb = HASH;                               \
    do {                                                   \
        uint group = hash_ & group_mask;                   \
        uint offset_in_group = hash_ & OFFSET_MASK;        \
        uint initial_offset = offset_in_group;             \
        do {                                               \
            uint32_t R_SLOT = group | offset_in_group;

#define ITER_SLOTS_END                                             \
            offset_in_group = (offset_in_group + 1) & OFFSET_MASK; \
        } while (offset_in_group != initial_offset);               \
        perturb >>= PERTURB_SHIFT;                                 \
        hash_ = hash_ * 5 + 1 + perturb;                           \
    } while (true);

// clang-format on

void iter_slots(uint32_t hash, uint32_t exponent) {
    uint32_t group_mask =
        ((1 << exponent) - 1) & ~OFFSET_MASK;

    ITER_SLOTS_BEGIN(hash, group_mask, slot) {
        std::cout << "Slot: " << slot << '\n';
        if (slot == 0) return;
    }
    ITER_SLOTS_END
}

int main(int argc, char const *argv[]) {
    srand(time(NULL));
    uint32_t hash = rand();
    iter_slots(hash, 4);
    return 0;
}

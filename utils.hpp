#pragma once

#include <stdint.h>
#include <vector>

#define LIKEKLY(x) __builtin_expect((x), 1)
#define UNLIKELY(x) __builtin_expect((x), 0)

#define REAL_NOINLINE __attribute__((noinline))

#if 0
#define NOINLINE REAL_NOINLINE
#else
#define NOINLINE
#endif

/* http://supertech.csail.mit.edu/papers/debruijn.pdf */

inline uint8_t get_bit_identifier_32(uint32_t v) {
    return ((uint32_t)(v * 0x07C4ACDD)) >> 27;
}

inline uint8_t get_bit_identifier_16(uint16_t v) {
    return ((uint16_t)(v * 0x0F65)) >> 12;
}

inline uint8_t get_bit_index_16(uint16_t v) {
    static const uint8_t table[16] = {0, 1, 11, 2,  14, 12,
                                      8, 3, 15, 10, 13, 7,
                                      9, 6, 5,  4};
    uint8_t identifier = ((uint16_t)(v * 0x0F65)) >> 12;
    uint8_t index = table[identifier];
    return index;
}

inline uint8_t get_bit_identifier_8(uint8_t v) {
    return ((uint8_t)(v * 0x1C)) >> 5;
}

inline uint16_t keep_one_bit(uint16_t v) {
    return v & -v;
}

inline uint8_t count_bits(uint32_t n) {
    uint8_t count = 0;
    while (n) {
        count += n & 1;
        n >>= 1;
    }
    return count;
}

inline constexpr uint32_t
next_multiple(uint32_t multiple_of, uint32_t value) {
    uint32_t result = 0;
    while (result < value) {
        result += multiple_of;
    }
    return result;
}

template <typename T>
void destroy_n(T *ptr, uint32_t amount) {
    for (uint32_t i = 0; i < amount; i++) {
        ptr[i].~T();
    }
}

template <typename T>
void partial_sort(T *data, uint32_t *keys, uint32_t length,
                  uint8_t shift, uint8_t digits) {
    uint32_t bucket_amount = 1 << digits;
    std::vector<uint32_t> sizes(bucket_amount);
    uint32_t mask = (1 << digits) - 1;

    for (uint32_t i = 0; i < length; i++) {
        uint32_t index = (keys[i] >> shift) & mask;
        sizes[index]++;
    }

    std::vector<uint32_t> offsets(bucket_amount);
    for (uint32_t i = 0; i < bucket_amount - 1; i++) {
        offsets[i + 1] = offsets[i] + sizes[i];
    }

    std::vector<T> tmp_data(
        std::make_move_iterator(data),
        std::make_move_iterator(data + length));
    std::vector<uint32_t> tmp_keys(keys, keys + length);

    for (uint32_t i = 0; i < length; i++) {
        uint32_t bucket = (tmp_keys[i] >> shift) & mask;
        uint32_t new_index = offsets[bucket];
        data[new_index] = std::move(tmp_data[i]);
        keys[new_index] = tmp_keys[i];
        offsets[bucket]++;
    }
}
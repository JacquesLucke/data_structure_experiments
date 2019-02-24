#include "utils.hpp"
#include <random>
#include <stdint.h>

std::random_device rd;
std::mt19937 eng(rd());

class HashBits32 {
  private:
    uint32_t m, n;
    static constexpr uint8_t exp = 31;
    static constexpr uint64_t prime = (1LL << exp) - 1;
    static constexpr uint64_t bit_mask = (1LL << exp) - 1;

  public:
    HashBits32(uint32_t m, uint32_t n) : m(m), n(n) {}

    uint32_t operator()(uint32_t value) const {
        /* ignores highest bit */
        uint64_t x = m * (uint64_t)value + n;
        uint32_t x1 = (x >> exp) & bit_mask;
        uint32_t x2 = x & bit_mask;
        uint32_t s = x1 + x2;
        if (s > prime) s -= prime;
        return s;
    }

    static HashBits32 get_new() {
        std::uniform_int_distribution<> distr(1, prime);
        return HashBits32(distr(eng), distr(eng));
    }
};
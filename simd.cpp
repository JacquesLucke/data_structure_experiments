#include <emmintrin.h>
#include <immintrin.h>
#include <iostream>

int main() {
    __m128i a = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5);
    __m128i b = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5);

    __m128i result = _mm_cmpeq_epi8(a, b);

    for (int i = 0; i < 16; i++) {
        int value = _mm_extract_epi8(result, 15 - i);
        std::cout << "Index " << i << ": " << value << std::endl;
    }
    return 0;
}

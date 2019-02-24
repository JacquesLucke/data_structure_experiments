#include "utils.hpp"
#include <cstring>
#include <iostream>
#include <memory>
#include <stdint.h>
#include <stdlib.h>
#include <type_traits>

#include <smmintrin.h>

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

uint8_t count_bits(uint32_t n) {
    uint8_t count = 0;
    while (n) {
        count += n & 1;
        n >>= 1;
    }
    return count;
}

template <typename T, typename HashFunc>
class Group {
  public:
    static const uint8_t s_max_size = 12;

  private:
    char m_hash_bytes[s_max_size];
    uint16_t m_used_mask = 0x0;
    uint8_t m_count = 0;
    char m_values[sizeof(T) * s_max_size];

    Group(Group &group) = delete;

  public:
    /* can also be zero initialized */
    Group() = default;

    static void Reset(Group &group) {
        group.m_count = 0;
        group.m_used_mask = 0x0;
    }

    ~Group() {
        for (uint8_t i = 0; i < m_count; i++) {
            this->element_pointer(i)->~T();
        }
    }

    inline uint8_t size() const {
        return m_count;
    }

    inline bool try_insert_new(T &value,
                               uint8_t hash_byte) NOINLINE {
        if (this->is_full()) return false;
        this->insert_new__no_check(value, hash_byte);
        return true;
    }

    inline bool contains(const T &value,
                         uint8_t hash_byte) const NOINLINE {
        uint16_t match_mask =
            this->get_hash_bytes_mask(hash_byte);

        while (match_mask != 0) {
            uint16_t single_bit = keep_one_bit(match_mask);
            uint8_t position =
                this->get_bit_position(single_bit);
            if (this->position_contains_value(position,
                                              value)) {
                return true;
            }
            match_mask &= ~single_bit;
        }
        return false;
    }

    bool remove(const T &value,
                uint8_t hash_byte) NOINLINE {
        uint16_t match_mask =
            this->get_hash_bytes_mask(hash_byte);

        while (match_mask != 0) {
            uint16_t single_bit = keep_one_bit(match_mask);
            uint8_t position =
                this->get_bit_position(single_bit);
            if (this->position_contains_value(position,
                                              value)) {
                this->remove_position(position);
                return true;
            }
            match_mask &= ~single_bit;
        }
        return false;
    }

    void split(Group &g0, Group &g1,
               uint8_t decision_mask) NOINLINE {

        Group *dst[2] = {&g0, &g1};

        for (uint8_t position = 0; position < m_count;
             position++) {
            T &value = this->element_at(position);
            uint8_t hash_byte = m_hash_bytes[position];
            uint8_t dst_index =
                (hash_byte & decision_mask) != 0;
            dst[dst_index]->insert_new__no_check(
                std::move(value), hash_byte);
            value.~T();
        }
    }

    void update_hash_bytes(HashFunc hash_fn,
                           uint8_t shift) {
        for (uint8_t position = 0; position < m_count;
             position++) {
            T &value = this->element_at(position);
            uint8_t hash_byte = hash_fn(value) >> shift;
            m_hash_bytes[position] = hash_byte;
        }
    }

  private:
    inline void
    insert_new__no_check(T &value,
                         uint8_t hash_byte) NOINLINE {
        uint8_t position = m_count;
        m_hash_bytes[position] = hash_byte;
        this->store(position, value);
        this->increase_size();
    }

    inline void
    insert_new__no_check(T &&value,
                         uint8_t hash_byte) NOINLINE {
        uint8_t position = m_count;
        m_hash_bytes[position] = hash_byte;
        this->store(position, value);
        this->increase_size();
    }

    inline void remove_position(uint8_t position) NOINLINE {
        uint8_t last_position = m_count - 1;
        if (position < last_position) {
            this->move_element(last_position, position);
            m_hash_bytes[position] =
                m_hash_bytes[last_position];
        }
        this->element_pointer(last_position)->~T();
        this->decrease_size();
    }

    inline bool is_full() const {
        return m_count == s_max_size;
    }

    inline void increase_size() {
        m_used_mask |= 1 << m_count;
        m_count++;
    }

    inline void decrease_size() {
        m_used_mask >>= 1;
        m_count--;
    }

    inline uint16_t
    get_hash_bytes_mask(uint8_t short_hash) const {
        __m128i cmp_hash = _mm_set1_epi8(short_hash);
        __m128i all_hash_bytes = *(__m128i *)m_hash_bytes;
        __m128i byte_mask =
            _mm_cmpeq_epi8(all_hash_bytes, cmp_hash);
        uint16_t bit_mask = _mm_movemask_epi8(byte_mask);
        return bit_mask & m_used_mask;
    }

    inline uint8_t get_bit_position(uint16_t mask) const {
        return get_bit_index_16(mask);
    }

    inline bool
    position_contains_value(uint8_t position,
                            const T &value) const {
        T &stored_value = this->element_at(position);
        return value == stored_value;
    }

    inline void store(uint8_t position, T &value) {
        T *dst = this->element_pointer(position);
        std::uninitialized_copy(&value, &value + 1, dst);
    }

    inline void store(uint8_t position, T &&value) {
        T *dst = this->element_pointer(position);
        std::uninitialized_copy(&value, &value + 1, dst);
    }

    inline void move_element(uint8_t src_position,
                             uint8_t dst_position) {
        T *src_pointer =
            this->element_pointer(src_position);
        T *dst_pointer =
            this->element_pointer(dst_position);
        std::copy(std::make_move_iterator(src_pointer),
                  std::make_move_iterator(src_pointer + 1),
                  dst_pointer);
    }

    inline T &element_at(uint8_t position) const {
        return *this->element_pointer(position);
    }

    inline T *element_pointer(uint8_t position) const {
        return (T *)m_values + position;
    }
};

template <typename T, typename HashFunc>
class HashSet {
  private:
    using GroupType = Group<T, HashFunc>;

    uint32_t m_group_mask = 0x0;
    uint32_t m_total_elements = 0;
    GroupType *m_groups;
    uint8_t m_size_exp = 0;
    uint8_t m_hash_byte_shift = 0;
    HashFunc m_hash_fn;

  public:
    HashSet() : m_hash_fn(HashFunc::get_new()) {
        m_groups =
            this->new_group_array(this->group_amount());
    }

    HashSet(std::initializer_list<T> values) : HashSet() {
        for (T value : values) {
            this->insert(value);
        }
    }

    inline uint32_t size() {
        return m_total_elements;
    }

    void insert(T &&value) {
        T val = value;
        this->insert(val);
    }

    void insert(T &value) {
        if (!this->contains(value)) {
            this->insert_new(value);
        }
    }

    void insert_new(T &&value) {
        T val = value;
        this->insert_new(val);
    }

    void insert_new(T &value) REAL_NOINLINE {
        uint32_t hash = this->calc_hash(value);
        uint8_t hash_byte = this->to_hash_byte(hash);
        while (!m_groups[this->group_index(hash)]
                    .try_insert_new(value, hash_byte)) {
            this->grow();
        }

        m_total_elements++;
    }

    bool contains(const T &value) NOINLINE {
        uint32_t hash = this->calc_hash(value);
        uint8_t hash_byte = this->to_hash_byte(hash);
        return m_groups[this->group_index(hash)].contains(
            value, hash_byte);
    }

    void remove(const T &&value) {
        T val = value;
        this->remove(val);
    }

    void remove(const T &value) NOINLINE {
        uint32_t hash = this->calc_hash(value);
        uint32_t index = this->group_index(hash);
        uint8_t hash_byte = this->to_hash_byte(hash);
        bool existed =
            m_groups[index].remove(value, hash_byte);
        if (existed) m_total_elements--;
    }

  private:
    inline uint32_t calc_hash(const T &value) const {
        return m_hash_fn(value);
    }

    inline uint8_t to_hash_byte(uint32_t hash) const {
        return hash >> m_hash_byte_shift;
    }

    inline uint32_t group_amount() const {
        return 1 << m_size_exp;
    }

    inline uint32_t group_index(uint32_t hash) const {
        return hash & m_group_mask;
    }

    float fullness() const {
        return this->m_total_elements /
               (float)(GroupType::s_max_size *
                       this->group_amount());
    }

    void grow() REAL_NOINLINE {
        if (m_size_exp % 3 == 0 && m_size_exp > 0) {
            m_hash_byte_shift = m_size_exp;
            for (uint32_t i = 0; i < this->group_amount();
                 i++) {
                m_groups[i].update_hash_bytes(
                    m_hash_fn, m_hash_byte_shift);
            }
        }

        uint8_t decision_mask =
            1 << (m_size_exp - m_hash_byte_shift);

        GroupType *new_groups =
            this->new_group_array(this->group_amount() * 2);

        for (uint32_t i = 0; i < this->group_amount();
             i++) {
            GroupType &g0 = new_groups[i];
            GroupType &g1 =
                new_groups[this->group_amount() + i];

            m_groups[i].split(g0, g1, decision_mask);
        }

        this->free_group_array(m_groups);
        m_size_exp++;
        m_group_mask = (1 << m_size_exp) - 1;
        m_groups = new_groups;
    }

    GroupType *new_group_array(uint32_t amount) {
        GroupType *groups = (GroupType *)aligned_alloc(
            64, amount * sizeof(GroupType));
        this->reset_group_array(groups, amount);

        return groups;
    }

    void reset_group_array(GroupType *groups,
                           uint32_t length) {
        for (uint32_t i = 0; i < length; i++) {
            GroupType::Reset(groups[i]);
        }
    }

    void free_group_array(GroupType *groups) {
        std::free(groups);
    }
};
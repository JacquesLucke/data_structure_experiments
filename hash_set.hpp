#include "utils.hpp"
#include <cstring>
#include <iostream>
#include <memory>
#include <stdint.h>
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

template <typename T, uint32_t Hash_fn(const T &value)>
class Group {
    __m128i m_hash_bytes;
    uint16_t m_used_mask = 0;
    uint8_t m_count = 0;
    char m_values[sizeof(T) * 16];

    Group(Group &group) = delete;

  public:
    /* can also be zero initialized */
    Group() = default;

    ~Group() {
        for (uint8_t i = 0; i < m_count; i++) {
            this->element_pointer(i)->~T();
        }
    }

    inline uint8_t size() const {
        return m_count;
    }

    inline bool try_insert_new(T &value,
                               uint32_t hash) NOINLINE {
        if (this->is_full()) return false;
        this->insert_new__no_check(value, hash);
        return true;
    }

    inline bool contains(const T &value,
                         uint32_t hash) const NOINLINE {
        uint8_t hash_byte = this->to_hash_byte(hash);
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

    bool remove(const T &value, uint32_t hash) NOINLINE {
        uint8_t hash_byte = this->to_hash_byte(hash);
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
               uint32_t decision_mask) NOINLINE {

        Group *dst[2] = {&g0, &g1};

        for (uint position = 0; position < m_count;
             position++) {
            T &value = this->element_at(position);
            uint32_t hash = Hash_fn(value);
            uint8_t dst_index = (hash & decision_mask) != 0;
            dst[dst_index]->insert_new__no_check(
                std::move(value), hash);
            value.~T();
        }
    }

    void split_out(Group &dst,
                   uint32_t decision_mask) NOINLINE {
        for (uint position = 0; position < m_count;
             position++) {
            T &value = this->element_at(position);
            uint32_t hash = Hash_fn(value);
            bool do_split = (hash & decision_mask) != 0;
            if (do_split) {
                dst.insert_new__no_check(std::move(value),
                                         hash);
                this->remove_position(position);
                position--;
            }
        }
    }

  private:
    inline void
    insert_new__no_check(T &value, uint32_t hash) NOINLINE {
        uint8_t position = m_count;
        uint8_t short_hash = this->to_hash_byte(hash);
        this->set_hash_byte(position, short_hash);
        this->store(position, value);
        this->increase_size();
    }

    inline void
    insert_new__no_check(T &&value,
                         uint32_t hash) NOINLINE {
        uint8_t position = m_count;
        uint8_t short_hash = this->to_hash_byte(hash);
        this->set_hash_byte(position, short_hash);
        this->store(position, value);
        this->increase_size();
    }

    inline void remove_position(uint8_t position) NOINLINE {
        uint8_t last_position = m_count - 1;
        if (position < last_position) {
            this->move_element(last_position, position);
            this->copy_hash_byte(last_position, position);
        }
        this->element_pointer(last_position)->~T();
        this->decrease_size();
    }

    inline bool is_full() const {
        return m_count == 16;
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
        __m128i byte_mask =
            _mm_cmpeq_epi8(m_hash_bytes, cmp_hash);
        uint16_t bit_mask = _mm_movemask_epi8(byte_mask);
        return bit_mask & m_used_mask;
    }

    inline uint8_t to_hash_byte(uint32_t hash) const {
        return hash >> 24;
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

    inline void set_hash_byte(uint8_t position,
                              uint8_t hash_byte) const {
        uint8_t *hash_bytes = this->hash_bytes_ptr();
        hash_bytes[position] = hash_byte;
    }

    inline void copy_hash_byte(uint8_t src,
                               uint dst) const {
        uint8_t *hash_bytes = this->hash_bytes_ptr();
        hash_bytes[dst] = hash_bytes[src];
    }

    inline uint8_t *hash_bytes_ptr() const {
        return (uint8_t *)&m_hash_bytes;
    }
};

template <typename T, uint32_t Hash_fn(const T &value)>
class HashSet {
  private:
    using GroupType = Group<T, Hash_fn>;

    uint32_t m_group_mask = 0x0;
    uint32_t m_total_elements = 0;
    GroupType *m_groups;
    uint8_t m_size_exp = 0;

  public:
    HashSet() {
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
        while (!m_groups[this->group_index(hash)]
                    .try_insert_new(value, hash)) {
            this->grow();
        }

        m_total_elements++;
    }

    bool contains(const T &value) NOINLINE {
        uint32_t hash = this->calc_hash(value);
        return m_groups[this->group_index(hash)].contains(
            value, hash);
    }

    void remove(const T &&value) {
        T val = value;
        this->remove(val);
    }

    void remove(const T &value) NOINLINE {
        uint32_t hash = this->calc_hash(value);
        uint32_t index = this->group_index(hash);
        bool existed = m_groups[index].remove(value, hash);
        if (existed) m_total_elements--;
    }

  private:
    inline uint32_t calc_hash(const T &value) const {
        return Hash_fn(value);
    }

    inline uint32_t group_amount() const {
        return 1 << m_size_exp;
    }

    inline uint32_t group_index(uint32_t hash) const {
        return hash & m_group_mask;
    }

    float fullness() const {
        return this->m_total_elements /
               (float)(16 * this->group_amount());
    }

    void grow() REAL_NOINLINE {
        if (std::is_trivial<T>::value) {
            this->grow_trivially_relocateable();
        } else {
            this->grow_not_trivially_relocateable();
        }
    }

    void grow_not_trivially_relocateable() NOINLINE {
        GroupType *new_groups =
            this->new_group_array(this->group_amount() * 2);

        uint32_t decision_mask = 1 << m_size_exp;
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

    void grow_trivially_relocateable() NOINLINE {
        uint32_t old_group_amount = this->group_amount();
        uint32_t new_group_amount = old_group_amount * 2;
        m_groups = this->realloc_group_array(
            m_groups, old_group_amount, new_group_amount);

        uint32_t decision_mask = 1 << m_size_exp;
        for (uint32_t i = 0; i < old_group_amount; i++) {
            GroupType &g0 = m_groups[i];
            GroupType &g1 = m_groups[i + old_group_amount];
            g0.split_out(g1, decision_mask);
        }

        m_size_exp++;
        m_group_mask = (1 << m_size_exp) - 1;
    }

    GroupType *new_group_array(uint32_t amount) {
        GroupType *groups = (GroupType *)std::calloc(
            amount, sizeof(GroupType));
        return groups;
    }

    GroupType *realloc_group_array(GroupType *groups_orig,
                                   uint32_t old_amount,
                                   uint32_t amount) {
        GroupType *groups = (GroupType *)std::realloc(
            groups_orig, amount * sizeof(GroupType));

        std::memset(groups + old_amount, 0,
                    sizeof(GroupType) *
                        (amount - old_amount));
        return groups;
    }

    void free_group_array(GroupType *groups) {
        std::free(groups);
    }
};
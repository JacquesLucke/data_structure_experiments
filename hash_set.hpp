#include <iostream>
#include <memory>
#include <stdint.h>

#include <smmintrin.h>

/* http://supertech.csail.mit.edu/papers/debruijn.pdf */

inline uint8_t get_bit_identifier_32(uint32_t v) {
    return (v * 0x07C4ACDD) >> 27;
}

inline uint8_t get_bit_identifier_16(uint16_t v) {
    return ((uint16_t)(v * 0x0F65)) >> 12;
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
    uint8_t m_count = 0;
    __m128i m_short_hashes;
    uint16_t m_used_mask = 0;
    char m_values[sizeof(T) * 16];

    Group(Group &group) = delete;

  public:
    Group() = default;

    inline uint8_t size() const {
        return m_count;
    }

    inline bool try_insert_new(T &value, uint32_t hash) {
        if (this->is_full()) return false;
        this->insert_new__no_check(value, hash);
        return true;
    }

    inline bool contains(const T &value, uint32_t hash) const {
        uint8_t short_hash = this->to_short_hash(hash);
        uint16_t match_mask = this->get_matching_short_hashes_mask(short_hash);

        while (match_mask != 0) {
            uint16_t single_bit = keep_one_bit(match_mask);
            uint8_t key = this->key_of_bit(single_bit);
            if (this->key_has_value(key, value)) {
                return true;
            }
            match_mask &= ~single_bit;
        }
        return false;
    }

    void split(Group &g0, Group &g1, uint32_t decision_mask) {
        for (uint i = 0; i < m_count; i++) {
            uint8_t key = this->key_of_index(i);
            T &value = this->element_at(key);
            uint32_t hash = Hash_fn(value);
            if (hash & decision_mask) {
                g1.insert_new__no_check(value, hash);
            } else {
                g0.insert_new__no_check(value, hash);
            }
        }
    }

  private:
    inline void insert_new__no_check(T &value, uint32_t hash) {
        uint8_t short_hash = this->to_short_hash(hash);
        m_short_hashes = _mm_insert_epi8(m_short_hashes, short_hash, m_count);

        uint8_t key = this->key_of_index(m_count);
        this->store(key, value);

        this->increase_size();
    }

    inline bool is_full() const {
        return m_count == 16;
    }

    inline void increase_size() {
        m_used_mask |= 1 << m_count;
        m_count++;
    }

    inline uint16_t get_matching_short_hashes_mask(uint8_t short_hash) const {
        __m128i cmp_hash = _mm_set1_epi8(short_hash);
        __m128i byte_mask = _mm_cmpeq_epi8(m_short_hashes, cmp_hash);
        uint16_t bit_mask = _mm_movemask_epi8(byte_mask);
        return bit_mask & m_used_mask;
    }

    inline uint8_t to_short_hash(uint32_t hash) const {
        return hash >> 24;
    }

    inline uint8_t key_of_index(uint8_t index) const {
        return this->key_of_bit(1 << index);
    }
    inline uint8_t key_of_bit(uint16_t mask) const {
        return get_bit_identifier_16(mask);
    }

    inline bool key_has_value(uint8_t key, const T &value) const {
        T &stored_value = this->element_at(key);
        return value == stored_value;
    }

    inline void store(uint8_t key, T &value) {
        T *dst = this->element_pointer(key);
        std::uninitialized_copy(&value, &value + 1, dst);
    }

    inline T &element_at(uint8_t key) const {
        return *this->element_pointer(key);
    }

    inline T *element_pointer(uint8_t key) const {
        return (T *)m_values + key;
    }
};

template <typename T, uint32_t Hash_fn(const T &value)>
class HashSet {
  private:
    using GroupType = Group<T, Hash_fn>;

    uint8_t m_size_exp = 1;
    GroupType *m_groups;

    uint32_t m_total_elements = 0;

  public:
    HashSet() {
        m_groups = this->new_group_array(this->group_amount());
    }

    inline uint32_t size() {
        return m_total_elements;
    }

    void insert_new(T &&value) {
        T val = value;
        this->insert_new(val);
    }

    void insert_new(T &value) {
        uint32_t hash = this->calc_hash(value);
        while (!m_groups[this->group_index(hash)].try_insert_new(value, hash)) {
            this->grow();
        }

        m_total_elements++;
    }

    bool contains(const T &value) {
        uint32_t hash = this->calc_hash(value);
        return m_groups[this->group_index(hash)].contains(value, hash);
    }

  private:
    inline uint32_t calc_hash(const T &value) const {
        return Hash_fn(value);
    }

    inline uint32_t group_amount() const {
        return 1 << m_size_exp;
    }

    inline uint32_t group_index(uint32_t hash) const {
        return hash & ((1 << m_size_exp) - 1);
    }

    void grow() {
        GroupType *new_groups = this->new_group_array(this->group_amount() * 2);

        uint32_t decision_mask = 1 << m_size_exp;
        for (uint32_t i = 0; i < this->group_amount(); i++) {
            GroupType &g0 = new_groups[i];
            GroupType &g1 = new_groups[this->group_amount() + i];

            m_groups[i].split(g0, g1, decision_mask);
        }

        this->free_group_array(m_groups);
        m_size_exp++;
        m_groups = new_groups;
    }

    GroupType *new_group_array(uint32_t amount) {
        GroupType *groups =
            (GroupType *)std::malloc(amount * sizeof(GroupType));
        for (uint32_t i = 0; i < amount; i++) {
            new (groups + i) GroupType();
        }
        return groups;
    }

    void free_group_array(GroupType *groups) {
        std::free(groups);
    }
};
#pragma once

#include "utils.hpp"
#include <assert.h>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdlib.h>
#include <type_traits>
#include <vector>

#include <smmintrin.h>

template <typename T, typename HashFunc, int N>
class Group {
  public:
    static const uint8_t s_max_size = N;

  private:
    char m_hash_bytes[s_max_size];
    uint16_t m_used_mask;
    uint8_t m_count;
    char m_values[sizeof(T) * s_max_size];

    struct MeasureSize {
        char s1[sizeof(m_hash_bytes)];
        uint16_t s2;
        uint8_t s3;
        char s4[sizeof(m_values)];
    };

    static const uint32_t s_required_size =
        sizeof(MeasureSize);
    static const uint32_t s_pad_size =
        next_multiple(64, s_required_size) -
        s_required_size;
    char pad[s_pad_size];

  public:
    /* can also be zero initialized */
    Group() : m_used_mask(0x0), m_count(0) {}

    ~Group() {
        destroy_n(this->element_pointer(0), m_count);
    }

    Group(Group &other) {
        this->copy_metadata_from(other);
        std::uninitialized_copy_n(other.value_pointer(),
                                  m_count,
                                  this->value_pointer());
    }

    Group(Group &&other) {
        this->copy_metadata_from(other);
        std::uninitialized_copy_n(
            std::make_move_iterator(other.value_pointer()),
            m_count, this->value_pointer());
    }

    Group &operator=(const Group &other) {
        if (this == &other) {
            return *this;
        }
        destroy_n(this->element_pointer(0), m_count);
        this->copy_metadata_from(other);
        std::uninitialized_copy_n(other.value_pointer(),
                                  m_count,
                                  this->value_pointer());
        return *this;
    }

    Group &operator=(Group &&other) {
        if (this == &other) {
            return *this;
        }
        destroy_n(this->element_pointer(0), m_count);
        this->copy_metadata_from(other);
        std::uninitialized_copy_n(
            std::make_move_iterator(other.value_pointer()),
            m_count, this->value_pointer());
        return *this;
    }

    void copy_metadata_from(const Group &other) {
        std::memcpy(m_hash_bytes, other.m_hash_bytes,
                    s_max_size);
        m_used_mask = other.m_used_mask;
        m_count = other.m_count;
    }

    void copy_metadata_from(const Group &&other) {
        std::memcpy(m_hash_bytes, other.m_hash_bytes,
                    s_max_size);
        m_used_mask = other.m_used_mask;
        m_count = other.m_count;
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
        /* TODO: can crash in some cases */
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

    inline T *value_pointer() const {
        return this->element_pointer(0);
    }

    inline T *element_pointer(uint8_t position) const {
        return (T *)m_values + position;
    }

    template <typename, typename>
    friend class HashSet;
};

template <typename T, typename HashFunc>
class HashSet {
  private:
    using GroupType = typename std::conditional<
        sizeof(T) == 4, Group<T, HashFunc, 12>,
        Group<T, HashFunc, 6>>::type;
    class GroupArray;

    uint32_t m_total_elements = 0;
    uint8_t m_hash_byte_shift = 0;
    HashFunc m_hash_fn;
    GroupArray m_groups;

  public:
    HashSet()
        : m_hash_fn(HashFunc::get_new()),
          m_groups(GroupArray(1)) {}

    HashSet(std::initializer_list<T> values) : HashSet() {
        for (T value : values) {
            this->insert(value);
        }
    }

    HashSet(std::vector<T> &values) : HashSet() {
        uint32_t length =
            values.size() / GroupType::s_max_size;
        uint8_t exp = 1;
        while ((1 << exp) < length) {
            exp++;
        }
        exp++;

        m_groups = GroupArray(exp);
        m_hash_byte_shift = (exp / 3) * 3;

        std::vector<uint32_t> hashes =
            this->calc_hashes(values);

        partial_sort(values.data(), hashes.data(),
                     values.size(), 18, 6);
        for (uint32_t i = 0; i < values.size(); i++) {
            this->insert_new(values[i], hashes[i]);
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
        uint32_t hash = this->calc_hash(value);
        if (!this->contains(value, hash)) {
            this->insert_new(value, hash);
        }
    }

    void insert_new(T &&value) {
        T val = value;
        this->insert_new(val);
    }

    void insert_new(T &value) REAL_NOINLINE {
        uint32_t hash = this->calc_hash(value);
        this->insert_new(value, hash);
    }

    bool contains(const T &value) NOINLINE {
        uint32_t hash = this->calc_hash(value);
        return this->contains(value, hash);
    }

    void remove(const T &&value) {
        T val = value;
        this->remove(val);
    }

    void remove(const T &value) NOINLINE {
        uint32_t hash = this->calc_hash(value);
        uint8_t hash_byte = this->to_hash_byte(hash);
        uint32_t index = this->group_index(hash);
        GroupType &group = m_groups[index];
        bool existed = group.remove(value, hash_byte);
        if (existed) m_total_elements--;
    }

  private:
    void insert_new(T &value, uint32_t hash) {
        while (true) {
            uint8_t hash_byte = this->to_hash_byte(hash);
            uint32_t index = this->group_index(hash);
            GroupType &group = m_groups[index];
            if (group.try_insert_new(value, hash_byte)) {
                break;
            }
            this->grow();
        }
        m_total_elements++;
    }

    bool contains(const T &value, uint32_t hash) {
        uint8_t hash_byte = this->to_hash_byte(hash);
        uint32_t index = this->group_index(hash);
        GroupType &group = m_groups[index];
        return group.contains(value, hash_byte);
    }

    std::vector<uint32_t>
    calc_hashes(const std::vector<T> values) {
        std::vector<uint32_t> hashes(values.size());
        for (uint32_t i = 0; i < values.size(); i++) {
            uint32_t hash = this->calc_hash(values[i]);
            hashes[i] = hash;
        }
        return hashes;
    }

    inline uint32_t calc_hash(const T &value) const {
        return m_hash_fn(value);
    }

    inline uint8_t to_hash_byte(uint32_t hash) const {
        return hash >> m_hash_byte_shift;
    }

    inline uint32_t group_amount() const {
        return m_groups.size();
    }

    inline uint32_t group_index(uint32_t hash) const {
        return hash & m_groups.mask();
    }

    float fullness() const {
        return this->m_total_elements /
               (float)(GroupType::s_max_size *
                       this->group_amount());
    }

    void grow() REAL_NOINLINE {
        if (m_groups.size_exp() % 3 == 0 &&
            m_groups.size_exp() > 0) {
            m_hash_byte_shift = m_groups.size_exp();
            this->recalculate_hash_bytes();
        }

        uint8_t decision_mask =
            1 << (m_groups.size_exp() - m_hash_byte_shift);

        uint32_t old_group_amount = this->group_amount();

        GroupArray new_groups =
            GroupArray(m_groups.size_exp() + 1);

        for (uint32_t i = 0; i < old_group_amount; i++) {
            GroupType &g0 = new_groups[i];
            GroupType &g1 =
                new_groups[old_group_amount + i];

            m_groups[i].split(g0, g1, decision_mask);
        }

        m_groups = std::move(new_groups);
    }

    void recalculate_hash_bytes() {
        for (uint32_t i = 0; i < this->group_amount();
             i++) {
            m_groups[i].update_hash_bytes(
                m_hash_fn, m_hash_byte_shift);
        }
    }

    class GroupArray {
      private:
        GroupType *m_data;
        uint32_t m_length;
        uint32_t m_mask;
        uint8_t m_size_exp;

        GroupType *allocate(uint32_t length) {
            static_assert(sizeof(GroupType) % 64 == 0);
            return (GroupType *)aligned_alloc(
                64, length * sizeof(GroupType));
        }

      public:
        void settings_from_exp(uint8_t exp) {
            m_length = 1 << exp;
            m_mask = m_length - 1;
            m_size_exp = exp;
        }

        GroupArray(uint8_t size_exp) {
            this->settings_from_exp(size_exp);
            m_data = this->allocate(m_length);

            for (uint32_t i = 0; i < m_length; i++) {
                new (m_data + i) GroupType();
            }
        }

        ~GroupArray() {
            if (m_data != nullptr) {
                destroy_n(m_data, m_length);
                std::free(m_data);
            }
        }

        GroupArray(const GroupArray &other) {
            this->settings_from_exp(other.m_size_exp);
            m_data = this->allocate(m_length);
            std::uninitialized_copy_n(other.m_data,
                                      m_length, m_data);
        }

        GroupArray(GroupArray &&other) {
            this->settings_from_exp(other.m_size_exp);
            m_data = other.m_data;
            other.m_data = nullptr;
        }

        GroupArray &operator=(const GroupArray &other) {
            if (this == &other) {
                return *this;
            }
            destroy_n(m_data, m_length);
            std::free(m_data);
            this->settings_from_exp(other.m_size_exp);
            m_data = this->allocate(m_length);
            std::uninitialized_copy_n(other.m_data,
                                      m_length, m_data);
            return *this;
        }

        GroupArray &operator=(GroupArray &&other) {
            if (this == &other) {
                return *this;
            }
            destroy_n(m_data, m_length);
            std::free(m_data);
            this->settings_from_exp(other.m_size_exp);
            m_data = other.m_data;
            other.m_length = 0;
            other.m_data = nullptr;
            return *this;
        }

        GroupType &operator[](const uint32_t index) const {
            return m_data[index];
        }

        uint8_t size_exp() const {
            return m_size_exp;
        }

        uint32_t size() const {
            return m_length;
        }

        uint32_t mask() const {
            return m_mask;
        }
    };

  public:
    /*************** Iterator *******************/

    class Iterator {
      private:
        const HashSet &m_set;
        uint32_t m_group_index;
        uint8_t m_position;

      public:
        Iterator(const HashSet &set, uint32_t group_index,
                 uint8_t position)
            : m_set(set), m_group_index(group_index),
              m_position(position) {}

        Iterator &operator++() {
            m_position++;

            while (true) {
                GroupType &group = this->group();
                if (m_position < group.size()) {
                    break;
                }
                m_position = 0;
                m_group_index++;
                if (m_group_index >= m_set.group_amount()) {
                    break;
                }
            }
            return *this;
        }

        Iterator operator++(int) {
            Iterator it = *this;
            ++*this;
            return it;
        }

        bool operator!=(const Iterator &it) const {
            return m_group_index != it.m_group_index ||
                   m_position != it.m_position;
        }

        const T &operator*() const {
            return this->group().element_at(m_position);
        }

        GroupType &group() const {
            return m_set.m_groups[m_group_index];
        }
    };

    Iterator begin() const {
        for (uint32_t i = 0; i < this->group_amount();
             i++) {
            if (m_groups[i].size() > 0) {
                return Iterator(*this, i, 0);
            }
        }
        return this->end();
    }

    Iterator end() const {
        return Iterator(*this, this->group_amount(), 0);
    }
};
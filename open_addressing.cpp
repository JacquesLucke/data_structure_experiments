#include <iostream>
#include <algorithm>
#include <memory>
#include <string>
#include <array>
#include <cassert>
#include <random>
#include <cstdlib>
#include <vector>
#include <stdint.h>
#include <xmmintrin.h>
#include "timeit.hpp"

/* The probing strategy is as follows:
 *   hash = compute_hash(value);
 *   perturb = hash;
 *   while (true) {
 *       group_index = (hash & mask) >> 2;
 *       offset = hash & 3;
 *       initial_offset = offset;
 *       do {
 *           handle_and_possibly_return(group_index, offset);
 *           offset = (offset + 1) & 3;
 *       } while (offset != initial_offset);
 *       perturb >>= 5;
 *       hash = hash * 5 + 1 + perturb;
 *   }
 *
 * Features of this strategy:
 *    1. Cache friendly. The inner loop always checks 4 items that are (usually) on the same cache
 * line.
 *    2. Can deal with bad/trivial hash functions. Eventually, all bits of the computed hash will
 * have an impact. This is because the variable perturb is shifted and mixed into the hash.
 * Therefore, clustering as known from linear probing should not happen.
 *    3. Hits every slot in the table. This is important to guarantee correctness. Basically, when
 * there is an empty slot it will be found. This is achieved using the hash * 5 + 1 part. The
 * perturb will be 0 eventually.
 *    4. The first few collisions (the common case) are handled very cheaply.
 *    5. Naturally supports values being grouped together to avoid padding when they are
 * interleaved.
 */

template<typename T> class MyHash {
};

template<> class MyHash<int> {
 public:
  uint32_t operator()(int value)
  {
    return (uint32_t)value;
  }
};

template<> class MyHash<std::string> {
 public:
  uint32_t operator()(const std::string &value)
  {
    uint32_t hash = 1331;
    for (char c : value) {
      hash = hash * 33 + c;
    }
    return hash;
  }
};

template<typename T> void uninitialized_copy_1(const T *from, T *to)
{
  std::uninitialized_copy_n(from, 1, to);
}

template<typename T> void uninitialized_move_1(T *from, T *to)
{
  std::uninitialized_copy_n(std::make_move_iterator(from), 1, to);
}

constexpr unsigned floorlog2(unsigned x)
{
  return x == 1 ? 0 : 1 + floorlog2(x >> 1);
}

constexpr unsigned ceillog2(unsigned x)
{
  return x == 1 ? 0 : floorlog2(x - 1) + 1;
}

template<> class MyHash<void *> {
 public:
  uint32_t operator()(void *value)
  {
    return ((uint64_t)value) >> 2;
  }
};

template<typename T> class MyHash<T *> {
 public:
  uint32_t operator()(T *value)
  {
    return (uint32_t)value >> ceillog2(alignof(T));
  }
};

uint32_t prime_numbers[] = {
    1,        3,        7,         13,        31,        61,         127,        251,
    509,      1021,     2039,      4093,      8191,      16381,      32749,      65521,
    131071,   262139,   524287,    1048573,   2097143,   4194301,    8388593,    16777213,
    33554393, 67108859, 134217689, 268435399, 536870912, 1073741789, 2147483647, 4294967291};

template<typename SlotGroup, uint32_t GroupsInSmallStorage = 1> class GroupedOpenAddressingArray {
 private:
  static constexpr auto slots_per_group = SlotGroup::slots_per_group;

  SlotGroup *m_groups;
  uint32_t m_group_amount;
  uint8_t m_group_exponent;
  uint32_t m_slots_total;
  uint32_t m_slots_set_or_dummy;
  uint32_t m_slots_dummy;
  uint32_t m_slot_mask;
  char m_local_storage[sizeof(SlotGroup) * GroupsInSmallStorage];

 public:
  explicit GroupedOpenAddressingArray(uint8_t group_exponent = 0)
  {
    m_slots_total = (1 << group_exponent) * slots_per_group;
    m_slots_set_or_dummy = 0;
    m_slots_dummy = 0;
    m_slot_mask = m_slots_total - 1;
    m_group_amount = m_slots_total / slots_per_group;
    m_group_exponent = group_exponent;

    if (m_group_amount <= GroupsInSmallStorage) {
      m_groups = this->small_storage();
    }
    else {
      m_groups = reinterpret_cast<SlotGroup *>(malloc(m_group_amount * sizeof(SlotGroup)));
    }

    for (uint32_t i = 0; i < m_group_amount; i++) {
      new (m_groups + i) SlotGroup();
    }
  }

  ~GroupedOpenAddressingArray()
  {
    if (m_groups != nullptr) {
      for (uint32_t i = 0; i < m_group_amount; i++) {
        m_groups[i].~SlotGroup();
      }
      if (!this->is_in_small_storage()) {
        free(static_cast<void *>(m_groups));
      }
    }
  }

  GroupedOpenAddressingArray(const GroupedOpenAddressingArray &other)
  {
    m_slots_total = other.m_slots_total;
    m_slots_set_or_dummy = other.m_slots_set_or_dummy;
    m_slots_dummy = other.m_slots_dummy;
    m_slot_mask = other.m_slot_mask;
    m_group_amount = other.m_group_amount;
    m_group_exponent = other.m_group_exponent;

    if (m_group_amount <= GroupsInSmallStorage) {
      m_groups = this->small_storage();
    }
    else {
      m_groups = static_cast<SlotGroup *>(malloc(m_group_amount * sizeof(SlotGroup)));
    }

    std::uninitialized_copy_n(other.m_groups, m_group_amount, m_groups);
  }

  GroupedOpenAddressingArray(GroupedOpenAddressingArray &&other)
  {
    m_slots_total = other.m_slots_total;
    m_slots_set_or_dummy = other.m_slots_set_or_dummy;
    m_slots_dummy = other.m_slots_dummy;
    m_slot_mask = other.m_slot_mask;
    m_group_amount = other.m_group_amount;
    m_group_exponent = other.m_group_exponent;
    if (other.is_in_small_storage()) {
      m_groups = this->small_storage();
      std::uninitialized_copy_n(std::make_move_iterator(other.m_groups), m_group_amount, m_groups);
      for (uint32_t i = 0; i < other.m_group_amount; i++) {
        other.m_groups[i].~SlotGroup();
      }
    }
    else {
      m_groups = other.m_groups;
    }

    other.m_groups = nullptr;
    other.~GroupedOpenAddressingArray();
    new (&other) GroupedOpenAddressingArray();
  }

  GroupedOpenAddressingArray &operator=(const GroupedOpenAddressingArray &other)
  {
    if (this == &other) {
      return *this;
    }
    this->~GroupedOpenAddressingArray();
    new (this) GroupedOpenAddressingArray(other);
    return *this;
  }

  GroupedOpenAddressingArray &operator=(GroupedOpenAddressingArray &&other)
  {
    if (this == &other) {
      return *this;
    }
    this->~GroupedOpenAddressingArray();
    new (this) GroupedOpenAddressingArray(std::move(other));
    return *this;
  }

  GroupedOpenAddressingArray init_reserved(uint32_t min_usable_slots) const
  {
    uint8_t group_exponent = ceillog2(min_usable_slots / slots_per_group + 1) + 1;
    GroupedOpenAddressingArray grown(group_exponent);
    grown.m_slots_set_or_dummy = this->slots_set();
    return grown;
  }

  uint32_t slots_total() const
  {
    return m_slots_total;
  }

  uint32_t slots_set() const
  {
    return m_slots_set_or_dummy - m_slots_dummy;
  }

  void update__empty_to_set()
  {
    m_slots_set_or_dummy++;
  }

  void update__dummy_to_set()
  {
    m_slots_dummy--;
  }

  void update__set_to_dummy()
  {
    m_slots_dummy++;
  }

  uint32_t slot_mask() const
  {
    return m_slot_mask;
  }

  const SlotGroup &group(uint32_t group_index) const
  {
    return m_groups[group_index];
  }

  SlotGroup &group(uint32_t group_index)
  {
    return m_groups[group_index];
  }

  uint8_t group_exponent() const
  {
    return m_group_exponent;
  }

  uint32_t group_amount() const
  {
    return m_group_amount;
  }

  bool should_grow() const
  {
    return m_slots_set_or_dummy >= m_slots_total / 2;
  }

  SlotGroup *begin()
  {
    return m_groups;
  }

  SlotGroup *end()
  {
    return m_groups + m_group_amount;
  }

  const SlotGroup *begin() const
  {
    return m_groups;
  }

  const SlotGroup *end() const
  {
    return m_groups + m_group_amount;
  }

 private:
  SlotGroup *small_storage() const
  {
    return reinterpret_cast<SlotGroup *>((char *)m_local_storage);
  }

  bool is_in_small_storage() const
  {
    return m_groups == this->small_storage();
  }
};

template<typename T> class Set {
 private:
  static constexpr uint32_t OFFSET_MASK = 3;
  static constexpr uint8_t IS_EMPTY = 0;
  static constexpr uint8_t IS_SET = 1;
  static constexpr uint8_t IS_DUMMY = 2;

  class Group {
   private:
    uint8_t m_status[4];
    char m_values[4 * sizeof(T)];

   public:
    static constexpr uint32_t slots_per_group = 4;

    Group()
    {
      m_status[0] = IS_EMPTY;
      m_status[1] = IS_EMPTY;
      m_status[2] = IS_EMPTY;
      m_status[3] = IS_EMPTY;
    }

    ~Group()
    {
      for (uint32_t offset = 0; offset < 4; offset++) {
        if (m_status[offset] == IS_SET) {
          this->value(offset)->~T();
        }
      }
    }

    Group(const Group &other)
    {
      for (uint32_t offset = 0; offset < 4; offset++) {
        uint8_t status = other.m_status[offset];
        m_status[offset] = status;
        if (status == IS_SET) {
          uninitialized_copy_1(other.value(offset), this->value(offset));
        }
      }
    }

    Group(Group &&other)
    {
      for (uint32_t offset = 0; offset < 4; offset++) {
        uint8_t status = other.m_status[offset];
        m_status[offset] = status;
        if (status == IS_SET) {
          uninitialized_move_1(other.value(offset), this->value(offset));
        }
      }
    }

    Group &operator=(const Group &other) = delete;
    Group &operator=(Group &&other) = delete;

    const uint8_t &status(uint32_t offset) const
    {
      return m_status[offset];
    }

    uint8_t &status(uint32_t offset)
    {
      return m_status[offset];
    }

    T *value(uint32_t offset) const
    {
      return (T *)(m_values + offset * sizeof(T));
    }

    void copy_in(uint32_t offset, const T &value)
    {
      assert(m_status[offset] != IS_SET);
      m_status[offset] = IS_SET;
      uninitialized_copy_1(&value, this->value(offset));
    }

    void move_in(uint32_t offset, T &value)
    {
      assert(m_status[offset] != IS_SET);
      m_status[offset] = IS_SET;
      uninitialized_move_1(&value, this->value(offset));
    }

    void set_dummy(uint32_t offset)
    {
      assert(m_status[offset] == IS_SET);
      m_status[offset] = IS_DUMMY;
      this->value(offset)->~T();
    }

    bool has_value(uint32_t offset, const T &value) const
    {
      return m_status[offset] == IS_SET && *this->value(offset) == value;
    }
  };

  GroupedOpenAddressingArray<Group> m_array = GroupedOpenAddressingArray<Group>();

 public:
  Set() = default;

  // clang-format off

#define ITER_SLOTS_BEGIN(VALUE, ARRAY, OPTIONAL_CONST, R_GROUP, R_OFFSET) \
  uint32_t hash = MyHash<T>{}(VALUE); \
  uint32_t perturb = hash; \
  while (true) { \
    uint32_t group_index = (hash & ARRAY.slot_mask()) >> 2; \
    uint8_t R_OFFSET = hash & OFFSET_MASK; \
    uint8_t initial_offset = R_OFFSET; \
    OPTIONAL_CONST Group &R_GROUP = ARRAY.group(group_index); \
    do {

#define ITER_SLOTS_END(R_OFFSET) \
      R_OFFSET = (R_OFFSET + 1) & OFFSET_MASK; \
    } while (R_OFFSET != initial_offset); \
    perturb >>= 5; \
    hash = hash * 5 + 1 + perturb; \
  } ((void)0)

  // clang-format on

  void reserve(uint32_t min_usable_slots)
  {
    this->grow(min_usable_slots);
  }

  void add_new(const T &value)
  {
    assert(!this->contains(value));
    this->ensure_can_add();

    ITER_SLOTS_BEGIN (value, m_array, , group, offset) {
      if (group.status(offset) == IS_EMPTY) {
        group.copy_in(offset, value);
        m_array.update__empty_to_set();
        return;
      }
    }
    ITER_SLOTS_END(offset);
  }

  void add_many(T *values, uint32_t amount)
  {
    constexpr uint32_t prefetch_distance = 6;
    constexpr uint32_t offset_factor = sizeof(Group) / 4;
    assert(sizeof(Group) % 4 == 0);
    uint32_t pipelined_adds = std::max<uint32_t>(amount - prefetch_distance - 1, 0);

    for (uint32_t i = 0; i < pipelined_adds; i++) {
      const T &prefetch_value = values[i + prefetch_distance];
      uint32_t hash = MyHash<T>{}(prefetch_value);
      uint32_t slot = hash & m_array.slot_mask();
      char *array_position = (char *)m_array.begin() + slot * offset_factor;
      _mm_prefetch(array_position, _MM_HINT_T0);

      this->add(values[i]);
    }

    for (uint32_t i = pipelined_adds; i < amount; i++) {
      this->add(values[i]);
    }
  }

  bool add(const T &value)
  {
    this->ensure_can_add();

    ITER_SLOTS_BEGIN (value, m_array, , group, offset) {
      uint8_t status = group.status(offset);
      if (status == IS_EMPTY) {
        group.copy_in(offset, value);
        m_array.update__empty_to_set();
        return true;
      }
      else if (group.has_value(offset, value)) {
        return false;
      }
    }
    ITER_SLOTS_END(offset);
  }

  bool contains(const T &value) const
  {
    ITER_SLOTS_BEGIN (value, m_array, const, group, offset) {
      uint8_t status = group.status(offset);
      if (status == IS_EMPTY) {
        return false;
      }
      else if (group.has_value(offset, value)) {
        return true;
      }
    }
    ITER_SLOTS_END(offset);
  }

  void remove(const T &value)
  {
    assert(this->contains(value));
    ITER_SLOTS_BEGIN (value, m_array, , group, offset) {
      if (group.has_value(offset, value)) {
        group.set_dummy(offset);
        m_array.update__set_to_dummy();
        return;
      }
    }
    ITER_SLOTS_END(offset);
  }

  uint32_t size() const
  {
    return m_array.slots_set();
  }

  void print_table() const
  {
    std::cout << "Hash Table:\n";
    std::cout << "  Size: " << m_array.slots_set() << '\n';
    std::cout << "  Capacity: " << m_array.slots_total() << '\n';
    uint32_t group_index = 0;
    for (const Group &group : m_array) {
      std::cout << "   Group: " << group_index++ << '\n';
      for (uint32_t offset = 0; offset < 4; offset++) {
        std::cout << "    " << offset << " \t";
        uint8_t status = group.status(offset);
        if (status == IS_EMPTY) {
          std::cout << "    <empty>\n";
        }
        else if (status == IS_SET) {
          const T &value = *group.value(offset);
          uint32_t collisions = this->count_collisions(value);
          std::cout << "    " << value << "  \t Collisions: " << collisions << '\n';
        }

        else if (status == IS_DUMMY) {
          std::cout << "    <dummy>\n";
        }
      }
    }
  }

 private:
  void ensure_can_add()
  {
    if (m_array.should_grow()) {
      this->grow(this->size() + 1);
    }
  }

  void grow(uint32_t min_usable_slots)
  {
    // std::cout << "Grow at " << m_array.slots_set() << '/' << m_array.slots_total() << '\n';
    GroupedOpenAddressingArray<Group> new_array = m_array.init_reserved(min_usable_slots);

    for (Group &old_group : m_array) {
      for (uint8_t offset = 0; offset < 4; offset++) {
        if (old_group.status(offset) == IS_SET) {
          this->add_after_grow(*old_group.value(offset), new_array);
        }
      }
    }

    m_array = std::move(new_array);
  }

  void add_after_grow(T &old_value, GroupedOpenAddressingArray<Group> &new_array)
  {
    ITER_SLOTS_BEGIN (old_value, new_array, , group, offset) {
      if (group.status(offset) == IS_EMPTY) {
        group.move_in(offset, old_value);
        return;
      }
    }
    ITER_SLOTS_END(offset);
  }

  uint32_t count_collisions(const T &value) const
  {
    uint32_t collisions = 0;
    ITER_SLOTS_BEGIN (value, m_array, const, group, offset) {
      if (group.status(offset) == IS_EMPTY || group.has_value(offset, value)) {
        return collisions;
      }
      collisions++;
    }
    ITER_SLOTS_END(offset);
  }

#undef ITER_SLOTS_BEGIN
#undef ITER_SLOTS_END
};

template<typename KeyT, typename ValueT> class Map {
 private:
  static constexpr uint32_t OFFSET_MASK = 3;
  static constexpr uint8_t IS_EMPTY = 0;
  static constexpr uint8_t IS_SET = 1;
  static constexpr uint8_t IS_DUMMY = 2;

  class Group {
   private:
    uint8_t m_status[4];
    char m_keys[4 * sizeof(KeyT)];
    char m_values[4 * sizeof(ValueT)];

   public:
    static constexpr uint32_t slots_per_group = 4;

    Group()
    {
      m_status[0] = IS_EMPTY;
      m_status[1] = IS_EMPTY;
      m_status[2] = IS_EMPTY;
      m_status[3] = IS_EMPTY;
    }

    ~Group()
    {
      for (uint32_t offset = 0; offset < 4; offset++) {
        if (m_status[offset] == IS_SET) {
          this->key(offset)->~KeyT();
          this->value(offset)->~ValueT();
        }
      }
    }

    Group(const Group &other)
    {
      for (uint32_t offset = 0; offset < 4; offset++) {
        uint8_t status = other.m_status[offset];
        m_status[offset] = status;
        if (status == IS_SET) {
          uninitialized_copy_1(other.key(offset), this->key(offset));
          uninitialized_copy_1(other.value(offset), this->value(offset));
        }
      }
    }

    Group(Group &&other)
    {
      for (uint32_t offset = 0; offset < 4; offset++) {
        uint8_t status = other.m_status[offset];
        m_status[offset] = status;
        if (status == IS_SET) {
          uninitialized_move_1(other.key(offset), this->key(offset));
          uninitialized_move_1(other.value(offset), this->value(offset));
        }
      }
    }

    bool has_key(uint32_t offset, const KeyT &key) const
    {
      return m_status[offset] == IS_SET && key == *this->key(offset);
    }

    KeyT *key(uint32_t offset) const
    {
      return (KeyT *)(m_keys + offset * sizeof(KeyT));
    }

    ValueT *value(uint32_t offset) const
    {
      return (ValueT *)(m_values + offset * sizeof(ValueT));
    }

    uint8_t status(uint32_t offset) const
    {
      return m_status[offset];
    }

    void copy_in(uint32_t offset, const KeyT &key, const ValueT &value)
    {
      assert(m_status[offset] != IS_SET);
      m_status[offset] = IS_SET;
      uninitialized_copy_1(&key, this->key(offset));
      uninitialized_copy_1(&value, this->value(offset));
    }

    void move_in(uint32_t offset, KeyT &key, ValueT &value)
    {
      assert(m_status[offset] != IS_SET);
      m_status[offset] = IS_SET;
      uninitialized_move_1(&key, this->key(offset));
      uninitialized_move_1(&value, this->value(offset));
    }

    void set_dummy(uint32_t offset)
    {
      assert(m_status[offset] == IS_SET);
      m_status[offset] = IS_DUMMY;
      this->key(offset)->~KeyT();
      this->value(offset)->~ValueT();
    }
  };

  GroupedOpenAddressingArray<Group> m_array;

 public:
  Map() = default;

  // clang-format off

#define ITER_SLOTS_BEGIN(KEY, ARRAY, OPTIONAL_CONST, R_GROUP, R_OFFSET) \
  uint32_t hash = MyHash<KeyT>{}(KEY); \
  uint32_t perturb = hash; \
  while (true) { \
    uint32_t group_index = (hash & ARRAY.slot_mask()) >> 2; \
    uint8_t R_OFFSET = hash & OFFSET_MASK; \
    uint8_t initial_offset = R_OFFSET; \
    OPTIONAL_CONST Group &R_GROUP = ARRAY.group(group_index); \
    do {

#define ITER_SLOTS_END(R_OFFSET) \
      R_OFFSET = (R_OFFSET + 1) & OFFSET_MASK; \
    } while (R_OFFSET != initial_offset); \
    perturb >>= 5; \
    hash = hash * 5 + 1 + perturb; \
  } ((void)0)

  // clang-format on

  void add_new(const KeyT &key, const ValueT &value)
  {
    assert(!this->contains(key));
    this->ensure_can_add();

    ITER_SLOTS_BEGIN (key, m_array, , group, offset) {
      if (group.status(offset) == IS_EMPTY) {
        group.copy_in(offset, key, value);
        m_array.update__empty_to_set();
        return;
      }
    }
    ITER_SLOTS_END(offset);
  }

  bool add(const KeyT &key, const ValueT &value)
  {
    this->ensure_can_add();

    ITER_SLOTS_BEGIN (key, m_array, , group, offset) {
      if (group.status(offset) == IS_EMPTY) {
        group.copy_in(offset, key, value);
        m_array.update__empty_to_set();
        return true;
      }
      else if (group.has_key(offset, key)) {
        return false;
      }
    }
    ITER_SLOTS_END(offset);
  }

  void remove(const KeyT &key)
  {
    assert(this->contains(key));
    ITER_SLOTS_BEGIN (key, m_array, , group, offset) {
      if (group.has_key(offset, key)) {
        group.set_dummy(offset);
        m_array.update__set_to_dummy();
        return;
      }
    }
    ITER_SLOTS_END(offset);
  }

  bool contains(const KeyT &key) const
  {
    ITER_SLOTS_BEGIN (key, m_array, const, group, offset) {
      if (group.status(offset) == IS_EMPTY) {
        return false;
      }
      else if (group.has_key(offset, key)) {
        return true;
      }
    }
    ITER_SLOTS_END(offset);
  }

  void print_table() const
  {
    std::cout << "Hash Table:\n";
    std::cout << "  Size: " << m_array.slots_set() << '\n';
    std::cout << "  Capacity: " << m_array.slots_total() << '\n';
    uint32_t group_index = 0;
    for (const Group &group : m_array) {
      std::cout << "   Group: " << group_index++ << '\n';
      for (uint32_t offset = 0; offset < 4; offset++) {
        std::cout << "    " << offset << " \t";
        uint8_t status = group.status(offset);
        if (status == IS_EMPTY) {
          std::cout << "    <empty>\n";
        }
        else if (status == IS_SET) {
          const KeyT &key = *group.key(offset);
          const ValueT &value = *group.value(offset);
          uint32_t collisions = this->count_collisions(value);
          std::cout << "    " << key << " -> " << value << "  \t Collisions: " << collisions
                    << '\n';
        }
        else if (status == IS_DUMMY) {
          std::cout << "    <dummy>\n";
        }
      }
    }
  }

 private:
  uint32_t count_collisions(const KeyT &key) const
  {
    uint32_t collisions = 0;
    ITER_SLOTS_BEGIN (key, m_array, const, group, offset) {
      if (group.status(offset) == IS_EMPTY || group.has_key(offset, key)) {
        return collisions;
      }
      collisions++;
    }
    ITER_SLOTS_END(offset);
  }

  void ensure_can_add()
  {
    if (m_array.should_grow()) {
      this->grow(this->size() + 1);
    }
  }

  void grow(uint32_t min_usable_slots)
  {
    GroupedOpenAddressingArray<Group> new_array = m_array.init_reserved(min_usable_slots);
    for (Group &old_group : m_array) {
      for (uint32_t offset = 0; offset < 4; offset++) {
        if (old_group.status(offset) == IS_SET) {
          this->add_after_grow(*old_group.key(offset), *old_group.value(offset), new_array);
        }
      }
    }
    m_array = std::move(new_array);
  }

  void add_after_grow(KeyT &key, ValueT &value, GroupedOpenAddressingArray<Group> &new_array)
  {
    ITER_SLOTS_BEGIN (key, new_array, , group, offset) {
      if (group.status(offset) == IS_EMPTY) {
        group.move_in(offset, key, value);
        return;
      }
    }
    ITER_SLOTS_END(offset);
  }

#undef ITER_SLOTS_BEGIN
#undef ITER_SLOTS_END
};

template<typename T> struct PointerKeyInfo {
  static T *get_empty()
  {
    return nullptr;
  }

  static bool is_empty(T *ptr)
  {
    return ptr == nullptr;
  }

  static bool is_set(T *ptr)
  {
    return ptr > 1;
  }
};

template<typename KeyT, typename ValueT, typename KeyInfo> class KeyInfoMap {
 private:
  class Group {
   private:
    std::array<KeyT, 4> m_keys;
    char m_values[4 * sizeof(ValueT)];

   public:
    Group()
        : m_keys({KeyInfo::get_empty(),
                  KeyInfo::get_empty(),
                  KeyInfo::get_empty(),
                  KeyInfo::get_empty()})
    {
    }

    Group(const Group &other)
    {
      for (uint32_t offset = 0; offset < 4; offset++) {
        KeyT &key = other.key(offset);
        if (KeyInfo::is_set(key)) {
          uninitialized_copy_1(other.value(offset), this->value(offset));
        }
        uninitialized_copy_1(&key, this->key(offset));
      }
    }

    Group(Group &&other)
    {
      for (uint32_t offset = 0; offset < 4; offset++) {
        KeyT &key = other.key(offset);
        if (KeyInfo::is_set(key)) {
          uninitialized_move_1(other.value(offset), this->value(offset));
        }
        uninitialized_move_1(&key, this->key(offset));
      }
    }

    bool is_empty(uint32_t offset) const
    {
      return KeyInfo::is_empty(this->key(offset));
    }

    bool is_set(uint32_t offset)
    {
      return KeyInfo::is_set(this->key(offset));
    }

    bool is_set_key(uint32_t offset, const KeyT &key)
    {
      return KeyInfo::is_set(key) && key == this->key(offset);
    }

    void move_in(uint32_t offset, KeyT &key, ValueT &value)
    {
      assert(!this->is_set(offset));
      this->key(offset) = key;
      uninitialized_move_1(&value, this->value(offset));
    }

    KeyT &key(uint32_t offset) const
    {
      return *(KeyT *)(m_keys + offset * sizeof(KeyT));
    }

    ValueT *value(uint32_t offset) const
    {
      return (ValueT *)(m_values + offset * sizeof(ValueT));
    }
  };

  GroupedOpenAddressingArray<Group> m_array;

 public:
  KeyInfoMap() = default;

 private:
};

int main()
{
  uint32_t amount = 100'000'000;
  std::vector<int> numbers;
  numbers.reserve(amount);

  srand(1);
  {
    TIMEIT("compute random numbers");
    for (uint32_t i = 0; i < amount; i++) {
      numbers.push_back((rand() << 16) | rand());
    }
  }

  std::vector<uint32_t> test_cases = {
      100, 1'000, 10'000, 100'000, 1'000'000, 10'000'000, 100'000'000};

  for (uint32_t size : test_cases) {
    std::cout << "Amount: " << size << '\n';
    for (uint32_t iteration = 0; iteration < 10; iteration++) {
      Set<int> myset;
      myset.reserve(size);
      TIMEIT("insert in map");
      myset.add_many(&numbers[0], size);
      /* for (int i = 0; i < size; i++) {
         myset.add(numbers[i]);
       }*/
    }
  }

  int a;
  std::cin >> a;

  return 0;
}

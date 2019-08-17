#include <iostream>
#include <algorithm>
#include <memory>
#include <cassert>
#include <stdint.h>

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

template<typename SlotGroup> class GroupedOpenAddressingArray {
 private:
  SlotGroup *m_groups;
  uint32_t m_group_amount;
  int8_t m_group_exponent;
  uint32_t m_slots_total;
  uint32_t m_slots_set;
  uint32_t m_slot_mask;

 public:
  explicit GroupedOpenAddressingArray(int8_t group_exponent = -1)
  {
    if (group_exponent == -1) {
      m_slots_total = 0;
      m_slots_set = 0;
      m_slot_mask = 0;
      m_group_amount = 0;
      m_group_exponent = -1;
      m_groups = nullptr;
      return;
    }

    m_slots_total = (1 << group_exponent) * SlotGroup::slots_per_group;
    m_slots_set = 0;
    m_slot_mask = m_slots_total - 1;
    m_group_amount = m_slots_total / SlotGroup::slots_per_group;
    m_group_exponent = group_exponent;
    m_groups = reinterpret_cast<SlotGroup *>(malloc(m_group_amount * sizeof(SlotGroup)));

    for (uint32_t i = 0; i < m_group_amount; i++) {
      m_groups[i].init_empty();
    }
  }

  ~GroupedOpenAddressingArray()
  {
    if (m_groups != nullptr) {
      free(static_cast<void *>(m_groups));
    }
  }

  GroupedOpenAddressingArray(const GroupedOpenAddressingArray &other)
  {
    m_slots_total = other.m_slots_total;
    m_slots_set = other.m_slots_set;
    m_slot_mask = other.m_slot_mask;
    m_group_amount = other.m_group_amount;
    m_group_exponent = other.m_group_exponent;
    m_groups = static_cast<SlotGroup *>(malloc(m_group_amount * sizeof(SlotGroup)));

    std::uninitialized_copy_n(other.m_groups, m_group_amount, m_groups);
  }

  GroupedOpenAddressingArray(GroupedOpenAddressingArray &&other)
  {
    m_slots_total = other.m_slots_total;
    m_slots_set = other.m_slots_set;
    m_slot_mask = other.m_slot_mask;
    m_group_amount = other.m_group_amount;
    m_group_exponent = other.m_group_exponent;
    m_groups = other.m_groups;

    other.m_slots_total = 0;
    other.m_slots_set = 0;
    other.m_slot_mask = 0;
    other.m_group_amount = 0;
    other.m_group_exponent = -1;
    other.m_groups = nullptr;
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

  uint32_t slots_total() const
  {
    return m_slots_total;
  }

  uint32_t &slots_set()
  {
    return m_slots_set;
  }

  uint32_t slots_set() const
  {
    return m_slots_set;
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

  int8_t group_exponent() const
  {
    return m_group_exponent;
  }

  uint32_t group_amount() const
  {
    return m_group_amount;
  }

  bool should_grow() const
  {
    return m_slots_set >= m_slots_total / 2;
  }
};

template<typename T> class Set {
 private:
  static constexpr uint32_t OFFSET_MASK = 3;
  static constexpr uint8_t IS_EMPTY = 0;
  static constexpr uint8_t IS_SET = 1;

  struct Group {
    static constexpr uint32_t slots_per_group = 4;

    uint8_t m_status[4];
    char m_values[4 * sizeof(T)];

    void init_empty()
    {
      m_status[0] = 0;
      m_status[1] = 0;
      m_status[2] = 0;
      m_status[3] = 0;
    }

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
  };

  GroupedOpenAddressingArray<Group> m_array = GroupedOpenAddressingArray<Group>(1);

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

  void add_new(const T &value)
  {
    assert(!this->contains(value));
    this->ensure_can_add();

    ITER_SLOTS_BEGIN (value, m_array, , group, offset) {
      uint8_t &status = group.status(offset);
      if (status == IS_EMPTY) {
        status = IS_SET;
        std::uninitialized_copy_n(&value, 1, group.value(offset));
        m_array.slots_set()++;
        return;
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
      else if (status == IS_SET && *group.value(offset) == value) {
        return true;
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
    for (uint32_t group_index = 0; group_index < m_array.group_amount(); group_index++) {
      const Group &group = m_array.group(group_index);
      std::cout << "   Group: " << group_index << '\n';
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
        else {
          assert(false);
        }
      }
    }
  }

 private:
  bool ensure_can_add()
  {
    if (!m_array.should_grow()) {
      return false;
    }
    this->grow();
    return true;
  }

  void grow()
  {
    std::cout << "Grow at " << m_array.slots_set() << '/' << m_array.slots_total() << '\n';
    int8_t old_exponent = m_array.group_exponent();
    int8_t new_exponent = old_exponent + 1;

    GroupedOpenAddressingArray<Group> new_array(new_exponent);

    for (uint32_t group_index = 0; group_index < m_array.group_amount(); group_index++) {
      Group &old_group = m_array.group(group_index);
      for (uint8_t offset = 0; offset < 4; offset++) {
        if (old_group.status(offset) == IS_SET) {
          this->reinsert_after_grow(*old_group.value(offset), new_array);
        }
      }
    }

    m_array = std::move(new_array);
  }

  void reinsert_after_grow(T &old_value, GroupedOpenAddressingArray<Group> &new_array)
  {
    ITER_SLOTS_BEGIN (old_value, new_array, , group, offset) {
      uint8_t &status = group.status(offset);
      if (status == IS_EMPTY) {
        status = IS_SET;
        std::uninitialized_copy_n(std::make_move_iterator(&old_value), 1, group.value(offset));
        old_value.~T();
        return;
      }
    }
    ITER_SLOTS_END(offset);
  }

  uint32_t count_collisions(const T &value) const
  {
    uint32_t collisions = 0;
    ITER_SLOTS_BEGIN (value, m_array, const, group, offset) {
      uint8_t status = group.status(offset);
      if (status == IS_EMPTY) {
        return collisions;
      }
      else if (status == IS_SET) {
        if (*group.value(offset) == value) {
          return collisions;
        }
        else {
          collisions++;
        }
      }
      else {
        assert(false);
      }
    }
    ITER_SLOTS_END(offset);
  }

#undef ITER_SLOTS_BEGIN
#undef ITER_SLOTS_END
};

int main()
{
  std::cout << "Start" << std::endl;
  Set<int> myset;
  for (int i = 0; i < 200; i++) {
    myset.add_new(i * 8);
  }
  myset.print_table();
  std::cout << "End\n";
}

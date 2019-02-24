#include "hash_set.hpp"
#include "hashing.hpp"
#include <gtest/gtest.h>

using IntSet = HashSet<int, HashBits32>;

TEST(HashSet, DefaultConstructor) {
    IntSet set;
    EXPECT_EQ(set.size(), 0);
}

TEST(HashSet, InitializerListConstructor) {
    IntSet set = {4, 5, 6};
    EXPECT_EQ(set.size(), 3);
}

TEST(HashSet, InsertNewIncreasesSize) {
    IntSet set;
    EXPECT_EQ(set.size(), 0);
    set.insert_new(4);
    EXPECT_EQ(set.size(), 1);
    set.insert_new(5);
    EXPECT_EQ(set.size(), 2);
}

TEST(HashSet, InsertExistingDoesNotIncreaseSize) {
    IntSet set = {1, 2};
    EXPECT_EQ(set.size(), 2);
    set.insert(2);
    EXPECT_EQ(set.size(), 2);
    set.insert(1);
    EXPECT_EQ(set.size(), 2);
}

TEST(HashSet, InsertNotExistingIncreasesSize) {
    IntSet set = {1, 2};
    EXPECT_EQ(set.size(), 2);
    set.insert(3);
    EXPECT_EQ(set.size(), 3);
    set.insert(0);
    EXPECT_EQ(set.size(), 4);
}

TEST(HashSet, ContainsExisting) {
    IntSet set = {4, 5, 6};
    EXPECT_TRUE(set.contains(4));
    EXPECT_TRUE(set.contains(5));
    EXPECT_TRUE(set.contains(6));
}

TEST(HashSet, ContainsNotExisting) {
    IntSet set = {4, 5, 6};
    EXPECT_FALSE(set.contains(3));
    EXPECT_FALSE(set.contains(10));
    EXPECT_FALSE(set.contains(7));
}

TEST(HashSet, RemoveExistingDecreasesSize) {
    IntSet set = {4, 5};
    EXPECT_EQ(set.size(), 2);
    set.remove(5);
    EXPECT_EQ(set.size(), 1);
    set.remove(4);
    EXPECT_EQ(set.size(), 0);
}

TEST(HashSet, ContainsAfterInsert) {
    IntSet set = {1, 2, 3};
    EXPECT_FALSE(set.contains(10));
    set.insert(10);
    EXPECT_TRUE(set.contains(10));
}

TEST(HashSet, DoesNotContainAfterRemove) {
    IntSet set = {1, 2, 3};
    EXPECT_TRUE(set.contains(2));
    set.remove(2);
    EXPECT_FALSE(set.contains(2));
}

TEST(HashSet, InsertMany) {
    IntSet set;
    int N = 1000;
    for (int i = 0; i < N; i += 4) {
        set.insert(i);
    }
    for (int i = 0; i < N; i++) {
        EXPECT_EQ(set.contains(i), (i % 4) == 0);
    }
}

TEST(HashSet, RemoveMany) {
    IntSet set;
    int N = 1000;
    for (int i = 0; i < N; i++) {
        set.insert(i);
    }
    EXPECT_EQ(set.size(), 1000);
    for (int i = 0; i < N; i += 5) {
        set.remove(i);
    }
    EXPECT_EQ(set.size(), 800);
    for (int i = 0; i < N; i++) {
        EXPECT_EQ(set.contains(i), (i % 5) != 0);
    }
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
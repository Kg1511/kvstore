#include "kvstore.hpp"

#include <gtest/gtest.h>

TEST(KVStoreTest, GetOnMissingKeyReturnsEmpty) {
    KVStore store;
    EXPECT_EQ(store.Get("missing"), "");
}

TEST(KVStoreTest, SetThenGetReturnsValue) {
    KVStore store;
    store.Set("name", "kartik");
    EXPECT_EQ(store.Get("name"), "kartik");
}

TEST(KVStoreTest, SetOverwritesExistingValue) {
    KVStore store;
    store.Set("name", "kartik");
    store.Set("name", "gupta");
    EXPECT_EQ(store.Get("name"), "gupta");
}

TEST(KVStoreTest, DelExistingKeyReturnsTrue) {
    KVStore store;
    store.Set("name", "kartik");
    EXPECT_TRUE(store.Del("name"));
}

TEST(KVStoreTest, DelMissingKeyReturnsFalse) {
    KVStore store;
    EXPECT_FALSE(store.Del("missing"));
}

TEST(KVStoreTest, GetAfterDelReturnsEmpty) {
    KVStore store;
    store.Set("name", "kartik");
    store.Del("name");
    EXPECT_EQ(store.Get("name"), "");
}
#include "core/UUID.h"

#include <gtest/gtest.h>

#include <set>
#include <unordered_set>

using DirtSim::UUID;

TEST(UUIDTest, NilIsAllZeros)
{
    UUID nil = UUID::nil();
    EXPECT_TRUE(nil.isNil());
    EXPECT_EQ(nil.toString(), "00000000-0000-0000-0000-000000000000");
}

TEST(UUIDTest, DefaultConstructorIsNil)
{
    UUID uuid;
    EXPECT_TRUE(uuid.isNil());
}

TEST(UUIDTest, GenerateIsNotNil)
{
    UUID uuid = UUID::generate();
    EXPECT_FALSE(uuid.isNil());
}

TEST(UUIDTest, GenerateIsUnique)
{
    std::set<std::string> seen;
    for (int i = 0; i < 1000; ++i) {
        UUID uuid = UUID::generate();
        std::string str = uuid.toString();
        EXPECT_EQ(seen.count(str), 0) << "Duplicate UUID: " << str;
        seen.insert(str);
    }
}

TEST(UUIDTest, GenerateIsVersion4)
{
    for (int i = 0; i < 100; ++i) {
        UUID uuid = UUID::generate();
        std::string str = uuid.toString();
        // Version 4 has '4' at position 14.
        EXPECT_EQ(str[14], '4') << "UUID: " << str;
        // Variant has 8, 9, a, or b at position 19.
        char variant = str[19];
        EXPECT_TRUE(variant == '8' || variant == '9' || variant == 'a' || variant == 'b')
            << "UUID: " << str << " variant: " << variant;
    }
}

TEST(UUIDTest, FromStringRoundTrip)
{
    std::string original = "550e8400-e29b-41d4-a716-446655440000";
    UUID uuid = UUID::fromString(original);
    EXPECT_EQ(uuid.toString(), original);
}

TEST(UUIDTest, FromStringInvalidLength)
{
    EXPECT_THROW(UUID::fromString("too-short"), std::invalid_argument);
    EXPECT_THROW(UUID::fromString("550e8400-e29b-41d4-a716-4466554400001"), std::invalid_argument);
}

TEST(UUIDTest, FromStringInvalidDashes)
{
    EXPECT_THROW(UUID::fromString("550e8400xe29b-41d4-a716-446655440000"), std::invalid_argument);
}

TEST(UUIDTest, FromStringInvalidHex)
{
    EXPECT_THROW(UUID::fromString("550e8400-e29b-41d4-a716-44665544000g"), std::invalid_argument);
}

TEST(UUIDTest, ToShortStringIsFirst8Chars)
{
    std::string full = "550e8400-e29b-41d4-a716-446655440000";
    UUID uuid = UUID::fromString(full);
    EXPECT_EQ(uuid.toShortString(), "550e8400");
}

TEST(UUIDTest, EqualityOperators)
{
    UUID a = UUID::fromString("550e8400-e29b-41d4-a716-446655440000");
    UUID b = UUID::fromString("550e8400-e29b-41d4-a716-446655440000");
    UUID c = UUID::fromString("550e8400-e29b-41d4-a716-446655440001");

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(UUIDTest, LessThanOrdering)
{
    UUID a = UUID::fromString("00000000-0000-0000-0000-000000000001");
    UUID b = UUID::fromString("00000000-0000-0000-0000-000000000002");

    EXPECT_LT(a, b);
    EXPECT_FALSE(b < a);
}

TEST(UUIDTest, WorksInStdSet)
{
    std::set<UUID> set;
    UUID a = UUID::generate();
    UUID b = UUID::generate();

    set.insert(a);
    set.insert(b);
    set.insert(a);

    EXPECT_EQ(set.size(), 2);
    EXPECT_TRUE(set.count(a) == 1);
    EXPECT_TRUE(set.count(b) == 1);
}

TEST(UUIDTest, WorksInUnorderedSet)
{
    std::unordered_set<UUID> set;
    UUID a = UUID::generate();
    UUID b = UUID::generate();

    set.insert(a);
    set.insert(b);
    set.insert(a);

    EXPECT_EQ(set.size(), 2);
    EXPECT_TRUE(set.count(a) == 1);
    EXPECT_TRUE(set.count(b) == 1);
}

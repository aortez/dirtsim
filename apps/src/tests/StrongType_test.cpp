#include "core/StrongType.h"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <unordered_set>

// Define test types.
using TestIdA = StrongType<struct TestIdATag>;
using TestIdB = StrongType<struct TestIdBTag>;

TEST(StrongTypeTest, DefaultConstructor)
{
    spdlog::info("Starting StrongTypeTest::DefaultConstructor test");
    TestIdA id;
    EXPECT_EQ(id.get(), 0);
}

TEST(StrongTypeTest, ExplicitConstructor)
{
    spdlog::info("Starting StrongTypeTest::ExplicitConstructor test");
    TestIdA id{ 42 };
    EXPECT_EQ(id.get(), 42);
}

TEST(StrongTypeTest, EqualityOperators)
{
    spdlog::info("Starting StrongTypeTest::EqualityOperators test");
    TestIdA a{ 10 };
    TestIdA b{ 10 };
    TestIdA c{ 20 };

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}

TEST(StrongTypeTest, ComparisonOperators)
{
    spdlog::info("Starting StrongTypeTest::ComparisonOperators test");
    TestIdA small{ 5 };
    TestIdA large{ 10 };

    EXPECT_TRUE(small < large);
    EXPECT_TRUE(small <= large);
    EXPECT_TRUE(large > small);
    EXPECT_TRUE(large >= small);

    TestIdA equal{ 5 };
    EXPECT_TRUE(small <= equal);
    EXPECT_TRUE(small >= equal);
    EXPECT_FALSE(small < equal);
    EXPECT_FALSE(small > equal);
}

TEST(StrongTypeTest, DifferentTypesDoNotCompile)
{
    spdlog::info("Starting StrongTypeTest::DifferentTypesDoNotCompile test");
    // This test documents that different tag types are distinct.
    // The following would not compile:
    // TestIdA a{10};
    // TestIdB b{10};
    // bool result = (a == b);  // Compile error: no match for operator==.

    // We verify they are different types at compile time.
    static_assert(
        !std::is_same_v<TestIdA, TestIdB>, "Different tags should create different types");
    SUCCEED();
}

TEST(StrongTypeTest, HashSupport)
{
    spdlog::info("Starting StrongTypeTest::HashSupport test");
    std::unordered_set<TestIdA> ids;

    ids.insert(TestIdA{ 1 });
    ids.insert(TestIdA{ 2 });
    ids.insert(TestIdA{ 3 });
    ids.insert(TestIdA{ 2 }); // Duplicate.

    EXPECT_EQ(ids.size(), 3);
    EXPECT_TRUE(ids.count(TestIdA{ 1 }) == 1);
    EXPECT_TRUE(ids.count(TestIdA{ 2 }) == 1);
    EXPECT_TRUE(ids.count(TestIdA{ 99 }) == 0);
}

TEST(StrongTypeTest, ConstexprUsage)
{
    spdlog::info("Starting StrongTypeTest::ConstexprUsage test");
    constexpr TestIdA id{ 100 };
    static_assert(id.get() == 100, "get() should work at compile time");
    static_assert(id == TestIdA{ 100 }, "operator== should work at compile time");
    static_assert(id < TestIdA{ 200 }, "operator< should work at compile time");
    SUCCEED();
}

TEST(StrongTypeTest, NegativeValues)
{
    spdlog::info("Starting StrongTypeTest::NegativeValues test");
    TestIdA negative{ -42 };
    TestIdA positive{ 42 };

    EXPECT_EQ(negative.get(), -42);
    EXPECT_TRUE(negative < positive);
    EXPECT_TRUE(negative < TestIdA{ 0 });
}

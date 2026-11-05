#include "TestUtils.h"

#include <limits>

using namespace MDL; // NOLINT(google-build-using-namespace)
using MDL::Tests::MustParse;

namespace {
enum class Blend : u8 {
    Opaque,
    AlphaBlend,
    Additive,
};
} // namespace


TEST(Value, Integers) {
    Document doc = MustParse(R"(
        small 42
        neg -42
        big 9223372036854775807
        min -9223372036854775808
        ubig 18446744073709551615
        over 18446744073709551616
        negzero -0
    )");

    EXPECT_EQ(doc["small"].As<i8>().value_or(0), 42);
    EXPECT_EQ(doc["small"].As<u32>().value_or(0), 42u);
    EXPECT_EQ(doc["neg"].As<i32>().value_or(0), -42);
    EXPECT_EQ(doc["big"].As<i64>().value_or(0), std::numeric_limits<i64>::max());
    EXPECT_EQ(doc["min"].As<i64>().value_or(0), std::numeric_limits<i64>::min());
    EXPECT_EQ(doc["ubig"].As<u64>().value_or(0), std::numeric_limits<u64>::max());
    EXPECT_EQ(doc["negzero"].As<u32>().value_or(1), 0u);

    EXPECT_EQ(doc["neg"].As<u32>().error().Code, ErrorCode::OutOfRange);
    EXPECT_EQ(doc["big"].As<i32>().error().Code, ErrorCode::OutOfRange);
    EXPECT_EQ(doc["ubig"].As<i64>().error().Code, ErrorCode::OutOfRange);
    EXPECT_EQ(doc["over"].As<u64>().error().Code, ErrorCode::OutOfRange);
    EXPECT_EQ(doc["small"].As<i8>().value_or(0), 42);
    EXPECT_FALSE(MustParse("a 200")["a"].As<i8>().has_value());
}

TEST(Value, FloatsTruncateToIntegers) {
    Document doc = MustParse(R"(
        pos 3.99
        neg -3.99
        exp 1.5e3
        tiny -0.5
        huge 1e30
    )");

    EXPECT_EQ(doc["pos"].As<i32>().value_or(0), 3);
    EXPECT_EQ(doc["neg"].As<i32>().value_or(0), -3);
    EXPECT_EQ(doc["exp"].As<u32>().value_or(0), 1500u);
    EXPECT_EQ(doc["tiny"].As<u32>().value_or(1), 0u);
    EXPECT_EQ(doc["neg"].As<u32>().error().Code, ErrorCode::OutOfRange);
    EXPECT_EQ(doc["huge"].As<i64>().error().Code, ErrorCode::OutOfRange);
}

TEST(Value, Floats) {
    Document doc = MustParse(R"(
        f 5.5
        i 7
        e -2.5e-3
        big 1e300
    )");

    EXPECT_DOUBLE_EQ(doc["f"].As<f64>().value_or(0), 5.5);
    EXPECT_FLOAT_EQ(doc["f"].As<f32>().value_or(0), 5.5f);
    EXPECT_DOUBLE_EQ(doc["i"].As<f64>().value_or(0), 7.0);
    EXPECT_DOUBLE_EQ(doc["e"].As<f64>().value_or(0), -0.0025);
    EXPECT_DOUBLE_EQ(doc["big"].As<f64>().value_or(0), 1e300);
    EXPECT_EQ(doc["big"].As<f32>().error().Code, ErrorCode::OutOfRange);
}

TEST(Value, Bools) {
    Document doc = MustParse("t true f false s \"true\" n 1");
    EXPECT_EQ(doc["t"].As<bool>().value_or(false), true);
    EXPECT_EQ(doc["f"].As<bool>().value_or(true), false);
    EXPECT_EQ(doc["s"].As<bool>().error().Code, ErrorCode::TypeMismatch);
    EXPECT_EQ(doc["n"].As<bool>().error().Code, ErrorCode::TypeMismatch);
}

TEST(Value, EverythingScalarIsAString) {
    Document doc = MustParse("n -1.50 b true s word");
    EXPECT_EQ(doc["n"].AsString().value_or(""), "-1.50");
    EXPECT_EQ(doc["b"].AsString().value_or(""), "true");
    EXPECT_EQ(doc["s"].As<std::string_view>().value_or(""), "word");

    Document containers = MustParse("o {} l []");
    EXPECT_EQ(containers["o"].AsString().error().Code, ErrorCode::TypeMismatch);
    EXPECT_EQ(containers["l"].AsString().error().Code, ErrorCode::TypeMismatch);
}

TEST(Value, StringsAreNotNumbers) {
    Document doc = MustParse("a \"12\" b word");
    EXPECT_EQ(doc["a"].As<int>().error().Code, ErrorCode::TypeMismatch);
    EXPECT_EQ(doc["b"].As<f32>().error().Code, ErrorCode::TypeMismatch);
}

TEST(Value, Enums) {
    Document doc = MustParse("a opaque b alpha_blend c nope d 1");
    EXPECT_EQ(doc["a"].As<Blend>().value_or(Blend::Additive), Blend::Opaque);
    EXPECT_EQ(doc["b"].As<Blend>().value_or(Blend::Additive), Blend::AlphaBlend);
    EXPECT_EQ(doc["c"].As<Blend>().error().Code, ErrorCode::InvalidEnum);
    EXPECT_EQ(doc["d"].As<Blend>().error().Code, ErrorCode::TypeMismatch);
    EXPECT_EQ(EnumToName(Blend::Additive), "additive");
}

TEST(Value, MissingValuesCarryParentPosition) {
    Document doc = MustParse("a 1\nobj {\n  x 1\n}\nlist [1 2]");

    Value missing = doc["obj"]["y"]["z"];
    EXPECT_FALSE(missing.IsValid());
    EXPECT_EQ(missing.GetType(), Type::None);

    Error error = missing.As<int>().error();
    EXPECT_EQ(error.Code, ErrorCode::MissingKey);
    EXPECT_EQ(error.Pos.Line, 2u); // "obj {" - where the lookup failed
    EXPECT_EQ(error.Pos.Column, 5u);

    EXPECT_EQ(doc["a"]["x"].As<int>().error().Code, ErrorCode::TypeMismatch);
    EXPECT_EQ(doc["list"][5].As<int>().error().Code, ErrorCode::IndexOutOfRange);
    EXPECT_EQ(doc["obj"][0].As<int>().error().Code, ErrorCode::TypeMismatch);

    EXPECT_TRUE(doc.Root().Contains("obj"));
    EXPECT_FALSE(doc.Root().Contains("nope"));
}

TEST(Value, ValueOr) {
    Document doc = MustParse("hp 100 name grom");
    EXPECT_EQ(doc["hp"].ValueOr(1), 100);
    EXPECT_EQ(doc["mp"].ValueOr(50), 50);
    EXPECT_EQ(doc["name"].ValueOr("none"), "grom");
    EXPECT_EQ(doc["title"].ValueOr("none"), "none");
    EXPECT_FLOAT_EQ(doc["speed"].ValueOr(5.5f), 5.5f);
    EXPECT_EQ(doc["hp"].ValueOr(Blend::Additive), Blend::Additive);
}

TEST(Value, Iteration) {
    Document doc = MustParse("o { a 1 b 2 } l [10 20 30]");

    int sum = 0;
    for (auto [key, value] : doc["o"].Members()) {
        sum += value.As<int>().value_or(0);
    }
    EXPECT_EQ(sum, 3);

    sum = 0;
    for (Value item : doc["l"].Items()) {
        sum += item.As<int>().value_or(0);
    }
    EXPECT_EQ(sum, 60);
    EXPECT_EQ(doc["l"].Items().size(), 3u);

    // iterating the wrong kind is just empty
    EXPECT_TRUE(doc["l"].Members().empty());
    EXPECT_TRUE(doc["missing"].Items().empty());
}

TEST(Value, Positions) {
    Document doc = MustParse("a 1\n  b {\n    c [x]\n  }");
    EXPECT_EQ(doc["a"].Pos().Line, 1u);
    EXPECT_EQ(doc["a"].Pos().Column, 3u);
    EXPECT_EQ(doc["b"].Pos().Line, 2u);
    EXPECT_EQ(doc["b"].Pos().Column, 5u);
    EXPECT_EQ(doc["b"]["c"][0].Pos().Line, 3u);
    EXPECT_EQ(doc["b"]["c"][0].Pos().Column, 8u);
}

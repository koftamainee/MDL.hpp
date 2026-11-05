#include "TestUtils.h"

#include <cmath>

using namespace MDL; // NOLINT(google-build-using-namespace)
using MDL::Tests::MustParse;
using MDL::Tests::ReadAsset;

namespace {

std::string WriteString(std::string_view value) {
    std::string text;
    Writer      w(Output::ToString(text));
    w.Field("v", value);
    EXPECT_TRUE(w.Finish().has_value());
    return text;
}

bool SameTree(Value a, Value b) {
    if (a.GetType() != b.GetType()) {
        return false;
    }
    switch (a.GetType()) {
        case Type::Object: {
            if (a.Size() != b.Size()) {
                return false;
            }
            for (auto [key, value] : a.Members()) {
                if (!SameTree(value, b[key])) {
                    return false;
                }
            }
            return true;
        }
        case Type::List: {
            if (a.Size() != b.Size()) {
                return false;
            }
            for (usize i = 0; i < a.Size(); ++i) {
                if (!SameTree(a[i], b[i])) {
                    return false;
                }
            }
            return true;
        }
        default:
            return a.AsString().value_or("") == b.AsString().value_or("");
    }
}

} // namespace


TEST(Writer, Streaming) {
    std::string text;
    Writer      w(Output::ToString(text));
    w.Field("shader", "pbr");
    w.BeginObject("pipeline");
    w.Field("depth_test", true);
    w.Field("blend", "opaque");
    w.EndObject();
    w.BeginList("tags").Item("sword").Item("shield").EndList();
    w.Key("speed").Number(5.5f);
    w.Key("hp").Number(100);
    ASSERT_TRUE(w.Finish().has_value());

    EXPECT_EQ(text,
              "shader pbr\n"
              "pipeline {\n"
              "  depth_test true\n"
              "  blend opaque\n"
              "}\n"
              "tags [sword shield]\n"
              "speed 5.5\n"
              "hp 100\n");
}

TEST(Writer, Layouts) {
    std::string text;
    Writer      w(Output::ToString(text));
    w.BeginList("regions");
    w.BeginObject(Layout::Inline).Field("name", "a").Field("x", 0).EndObject();
    w.BeginObject().Field("name", "b").BeginList("frames").Item(1).Item(2).EndList().EndObject();
    w.EndList();
    w.BeginObject("empty").EndObject();
    w.BeginList("none").EndList();
    w.BeginObject("inline", Layout::Inline).BeginObject("nested").Field("k", 1).EndObject().EndObject();
    ASSERT_TRUE(w.Finish().has_value());

    EXPECT_EQ(text,
              "regions [\n"
              "  { name a x 0 }\n"
              "  {\n"
              "    name b\n"
              "    frames [1 2]\n"
              "  }\n"
              "]\n"
              "empty {}\n"
              "none []\n"
              "inline { nested { k 1 } }\n");
}

TEST(Writer, ExplicitRootObjectAndTabs) {
    std::string  text;
    WriteOptions options;
    options.UseTabs = true;
    Writer w(Output::ToString(text), options);
    w.BeginObject();
    w.BeginObject("a").Field("b", 1).EndObject();
    w.EndObject();
    ASSERT_TRUE(w.Finish().has_value());
    EXPECT_EQ(text, "a {\n\tb 1\n}\n");
}

TEST(Writer, StringQuoting) {
    EXPECT_EQ(WriteString("plain"), "v plain\n");
    EXPECT_EQ(WriteString("shaders/pbr.slang"), "v shaders/pbr.slang\n");
    EXPECT_EQ(WriteString("hello world"), "v \"hello world\"\n");
    EXPECT_EQ(WriteString("#FFFFFF"), "v \"#FFFFFF\"\n");
    EXPECT_EQ(WriteString("123"), "v \"123\"\n");
    EXPECT_EQ(WriteString("-1.5e3"), "v \"-1.5e3\"\n");
    EXPECT_EQ(WriteString("true"), "v \"true\"\n");
    EXPECT_EQ(WriteString(""), "v \"\"\n");
    EXPECT_EQ(WriteString("a\nb\t\"c\""), "v \"a\\nb\\t\\\"c\\\"\"\n");
    EXPECT_EQ(WriteString(std::string_view("\x01", 1)), "v \"\\x01\"\n");
    EXPECT_EQ(WriteString("C:\\Projects\\Mantle"), "v C:\\Projects\\Mantle\n");
    EXPECT_EQ(WriteString("C:\\Program Files\\Mantle"), "v `C:\\Program Files\\Mantle`\n");
    EXPECT_EQ(WriteString("C:\\Projects\\"), "v \"C:\\\\Projects\\\\\"\n");
    EXPECT_EQ(WriteString("a\\b`c"), "v \"a\\\\b`c\"\n");
    EXPECT_EQ(WriteString("Гром"), "v Гром\n");
}

TEST(Writer, KeyQuoting) {
    std::string text;
    Writer      w(Output::ToString(text));
    w.Field("ok_key-1", 1).Field("needs quotes", 2).Field("1st", 3).Field("", 4);
    ASSERT_TRUE(w.Finish().has_value());
    EXPECT_EQ(text, "ok_key-1 1\n\"needs quotes\" 2\n\"1st\" 3\n\"\" 4\n");
}

TEST(Writer, Numbers) {
    std::string text;
    Writer      w(Output::ToString(text));
    w.Field("a", 5.0f).Field("b", 0.1f).Field("c", 0.1).Field("d", 1e20).Field("e", -0.0);
    w.Field("f", std::numeric_limits<i64>::min()).Field("g", std::numeric_limits<u64>::max());
    w.Field("h", static_cast<u8>(200)).Field("i", static_cast<i8>(-5));
    ASSERT_TRUE(w.Finish().has_value());
    EXPECT_EQ(text,
              "a 5.0\nb 0.1\nc 0.1\nd 1e+20\ne -0.0\n"
              "f -9223372036854775808\ng 18446744073709551615\nh 200\ni -5\n");

    Document doc = MustParse(text);
    EXPECT_FLOAT_EQ(doc["b"].As<f32>().value_or(0), 0.1f);
    EXPECT_EQ(doc["g"].As<u64>().value_or(0), std::numeric_limits<u64>::max());
}

TEST(Writer, Errors) {
    {
        std::string text;
        Writer      w(Output::ToString(text));
        w.BeginList("l").Key("oops");
        EXPECT_EQ(w.Finish().error().Code, ErrorCode::InvalidWriterState);
    }
    {
        std::string text;
        Writer      w(Output::ToString(text));
        w.BeginObject("o");
        EXPECT_EQ(w.Finish().error().Code, ErrorCode::InvalidWriterState);
    }
    {
        std::string text;
        Writer      w(Output::ToString(text));
        w.Key("a");
        w.Key("b");
        EXPECT_EQ(w.Finish().error().Code, ErrorCode::InvalidWriterState);
    }
    {
        std::string text;
        Writer      w(Output::ToString(text));
        w.String("no key at top level");
        EXPECT_EQ(w.Finish().error().Code, ErrorCode::InvalidWriterState);
    }
    {
        std::string text;
        Writer      w(Output::ToString(text));
        w.Field("nan", std::nan(""));
        EXPECT_EQ(w.Finish().error().Code, ErrorCode::NonFiniteNumber);
    }
}

TEST(Writer, ChunkedOutput) {
    std::string received;
    int         calls = 0;
    auto        sink = [&](std::string_view chunk) {
        ++calls;
        received += chunk;
    };

    Writer      w(Output::From(sink));
    std::string expected;
    for (int i = 0; i < 2000; ++i) {
        w.Field(std::format("key{}", i), i);
        expected += std::format("key{} {}\n", i, i);
    }
    ASSERT_TRUE(w.Finish().has_value());
    EXPECT_EQ(received, expected);
    EXPECT_GT(calls, 1); // buffered, flushed in several chunks
}

TEST(Writer, OutputFailure) {
    auto   sink = [](std::string_view) { return false; };
    Writer w(Output::From(sink));
    w.Field("a", 1);
    EXPECT_EQ(w.Finish().error().Code, ErrorCode::OutputFailed);
}

TEST(Writer, DomRoundTrip) {
    for (const char *asset : {"shader.mdl", "atlas.mdl", "character.mdl"}) {
        Document original = MustParse(ReadAsset(asset));

        auto first = WriteToString(original);
        ASSERT_TRUE(first.has_value()) << asset;

        Document reparsed = MustParse(*first);
        EXPECT_TRUE(SameTree(original.Root(), reparsed.Root())) << asset << "\n"
                                                                << *first;

        auto second = WriteToString(reparsed);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(*first, *second) << asset;
    }
}

TEST(Writer, DomLayout) {
    Document doc = MustParse(ReadAsset("atlas.mdl"));
    auto     text = WriteToString(doc);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text,
              "texture Images/Atlas\n"
              "regions [\n"
              "  { name slime-1 x 0 y 0 width 20 height 20 }\n"
              "  { name slime-2 x 0 y 20 width 20 height 20 }\n"
              "  { name bat-1 x 20 y 0 width 20 height 20 }\n"
              "  { name bat-2 x 20 y 20 width 20 height 20 }\n"
              "  { name bat-3 x 40 y 0 width 20 height 20 }\n"
              "]\n"
              "animations [\n"
              "  {\n"
              "    name slime-animation\n"
              "    delay 200\n"
              "    frames [slime-1 slime-2]\n"
              "  }\n"
              "  {\n"
              "    name bat-animation\n"
              "    delay 200\n"
              "    frames [bat-1 bat-2 bat-1 bat-3]\n"
              "  }\n"
              "]\n");
}

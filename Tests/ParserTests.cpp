#include "TestUtils.h"

using namespace MDL; // NOLINT(google-build-using-namespace)
using MDL::Tests::MustFail;
using MDL::Tests::MustParse;
using MDL::Tests::ReadAsset;


TEST(Parser, Empty) {
    Document doc = MustParse("");
    EXPECT_TRUE(doc.Root().IsObject());
    EXPECT_EQ(doc.Root().Size(), 0u);

    Document comments = MustParse("  # just a comment\n\n   # another");
    EXPECT_EQ(comments.Root().Size(), 0u);
}

TEST(Parser, ShaderSample) {
    std::string source = ReadAsset("shader.mdl");
    ASSERT_FALSE(source.empty());
    Document doc = MustParse(source);

    EXPECT_EQ(doc["shader"].AsString().value_or(""), "pbr");
    EXPECT_EQ(doc["stages"]["vertex"]["src"].AsString().value_or(""), "shaders/pbr.slang");
    EXPECT_EQ(doc["stages"]["fragment"]["entry"].AsString().value_or(""), "fs_main");
    EXPECT_EQ(doc["pipeline"]["depth_test"].As<bool>().value_or(false), true);
    EXPECT_EQ(doc["pipeline"]["cull_mode"].AsString().value_or(""), "back");

    Value list = doc["some_list"];
    ASSERT_TRUE(list.IsList());
    ASSERT_EQ(list.Size(), 2u);
    EXPECT_EQ(list[0].AsString().value_or(""), "Monday");
    EXPECT_EQ(list[1].AsString().value_or(""), "Tuesday");

    EXPECT_EQ(doc["string_with_spaces"].AsString().value_or(""), "Hello world!");
    EXPECT_EQ(doc["black_color"].AsString().value_or(""), "#FFFFFF");
    EXPECT_EQ(doc["windows_path"].AsString().value_or(""), "C:\\Projects\\Mantle");
    EXPECT_EQ(doc["hello2"].AsString().value_or(""), "Hello ` World!");

    EXPECT_EQ(doc["string_with_spaces"].Style(), StringStyle::Quoted);
    EXPECT_EQ(doc["windows_path"].Style(), StringStyle::Raw);
    EXPECT_EQ(doc["shader"].Style(), StringStyle::Bare);
}

TEST(Parser, AtlasSample) {
    Document doc = MustParse(ReadAsset("atlas.mdl"));

    EXPECT_EQ(doc["texture"].AsString().value_or(""), "Images/Atlas");

    Value regions = doc["regions"];
    ASSERT_EQ(regions.Size(), 5u);
    EXPECT_EQ(regions[2]["name"].AsString().value_or(""), "bat-1");
    EXPECT_EQ(regions[2]["x"].As<i32>().value_or(-1), 20);

    Value frames = doc["animations"][1]["frames"];
    ASSERT_EQ(frames.Size(), 4u);
    EXPECT_EQ(frames[3].AsString().value_or(""), "bat-3");
}

TEST(Parser, MemberOrderIsKept) {
    Document    doc = MustParse("c 1 a 2 b 3");
    std::string keys;
    for (auto [key, value] : doc.Root().Members()) {
        keys += key;
    }
    EXPECT_EQ(keys, "cab");
}

TEST(Parser, ScalarKinds) {
    Document doc = MustParse(R"(
        int 123
        neg -42
        flt 1.5
        exp 1e10
        exp2 -2.5E-3
        yes true
        no false
        word hello
        notnum 123abc
        dash -
        dot .5
        plus +5
        quotednum "123"
        True True
    )");

    EXPECT_EQ(doc["int"].GetType(), Type::Number);
    EXPECT_EQ(doc["neg"].GetType(), Type::Number);
    EXPECT_EQ(doc["flt"].GetType(), Type::Number);
    EXPECT_EQ(doc["exp"].GetType(), Type::Number);
    EXPECT_EQ(doc["exp2"].GetType(), Type::Number);
    EXPECT_EQ(doc["yes"].GetType(), Type::Bool);
    EXPECT_EQ(doc["no"].GetType(), Type::Bool);
    EXPECT_EQ(doc["word"].GetType(), Type::String);
    EXPECT_EQ(doc["notnum"].GetType(), Type::String);
    EXPECT_EQ(doc["dash"].GetType(), Type::String);
    EXPECT_EQ(doc["dot"].GetType(), Type::String);
    EXPECT_EQ(doc["plus"].GetType(), Type::String);
    EXPECT_EQ(doc["quotednum"].GetType(), Type::String);
    EXPECT_EQ(doc["True"].GetType(), Type::String);
}

TEST(Parser, Keys) {
    Document doc = MustParse(R"(
        plain 1
        with-dash_2 2
        "quoted key" 3
        `raw key` 4
        "esc\tkey" 5
    )");
    EXPECT_EQ(doc["plain"].As<int>().value_or(0), 1);
    EXPECT_EQ(doc["with-dash_2"].As<int>().value_or(0), 2);
    EXPECT_EQ(doc["quoted key"].As<int>().value_or(0), 3);
    EXPECT_EQ(doc["raw key"].As<int>().value_or(0), 4);
    EXPECT_EQ(doc["esc\tkey"].As<int>().value_or(0), 5);
}

TEST(Parser, InlineAndNoSeparators) {
    Document doc = MustParse("a{b 1 c[x y{z 2}]}d`r`e\"q\"");
    EXPECT_EQ(doc["a"]["b"].As<int>().value_or(0), 1);
    // "y{" is the bare string y followed by an object
    EXPECT_EQ(doc["a"]["c"].Size(), 3u);
    EXPECT_EQ(doc["a"]["c"][2]["z"].As<int>().value_or(0), 2);
    EXPECT_EQ(doc["d"].AsString().value_or(""), "r");
    EXPECT_EQ(doc["e"].AsString().value_or(""), "q");
}

TEST(Parser, CommentsEverywhere) {
    Document doc = MustParse(R"(
        # top
        obj { # after brace
            a 1 # after value
            # inside
        } # after close
        list [ # open
            1 # one
            2
        ]
    )");
    EXPECT_EQ(doc["obj"]["a"].As<int>().value_or(0), 1);
    EXPECT_EQ(doc["list"].Size(), 2u);
}

TEST(Parser, QuotedEscapes) {
    Document doc = MustParse(R"(
        simple "a\nb\tc\\d\"e\'f\?g"
        bells "\a\b\f\v\r"
        octal "\101\60\0x"
        hex "\x41\x4a\x4B\x7"
        unicode "\u00e9\u4F60\U0001F600"
        unknown "\q\d\%"
    )");

    EXPECT_EQ(doc["simple"].AsString().value_or(""), "a\nb\tc\\d\"e'f?g");
    EXPECT_EQ(doc["bells"].AsString().value_or(""), "\a\b\f\v\r");
    EXPECT_EQ(doc["octal"].AsString().value_or(""), std::string_view("A0\0x", 4));
    EXPECT_EQ(doc["hex"].AsString().value_or(""), "AJK\x07");
    EXPECT_EQ(doc["unicode"].AsString().value_or(""), "\xC3\xA9\xE4\xBD\xA0\xF0\x9F\x98\x80");
    EXPECT_EQ(doc["unknown"].AsString().value_or(""), "\\q\\d\\%");
}

TEST(Parser, InvalidEscapes) {
    EXPECT_EQ(MustFail(R"(a "\x")").Code, ErrorCode::InvalidEscape);
    EXPECT_EQ(MustFail(R"(a "\u12")").Code, ErrorCode::InvalidEscape);
    EXPECT_EQ(MustFail(R"(a "\uD800")").Code, ErrorCode::InvalidEscape);
    EXPECT_EQ(MustFail(R"(a "\U00110000")").Code, ErrorCode::InvalidEscape);
    EXPECT_EQ(MustFail(R"(a "\777")").Code, ErrorCode::InvalidEscape);

    Error error = MustFail("a 1\nbb \"ok \\uZZZZ\"");
    EXPECT_EQ(error.Pos.Line, 2u);
    EXPECT_EQ(error.Pos.Column, 8u);
}

TEST(Parser, RawStrings) {
    Document doc = MustParse("a `C:\\path\\to` b `multi\nline` c `tick \\` here` d ``");
    EXPECT_EQ(doc["a"].AsString().value_or(""), "C:\\path\\to");
    EXPECT_EQ(doc["b"].AsString().value_or(""), "multi\nline");
    EXPECT_EQ(doc["c"].AsString().value_or(""), "tick ` here");
    EXPECT_EQ(doc["d"].AsString().value_or("x"), "");
}

TEST(Parser, BareEscapes) {
    Document doc = MustParse(R"(
        color \#FFF
        braces a\{b\}c
        spaced hello\ world
        path C:\dir\file
        tick a\`b
    )");
    EXPECT_EQ(doc["color"].AsString().value_or(""), "#FFF");
    EXPECT_EQ(doc["braces"].AsString().value_or(""), "a{b}c");
    EXPECT_EQ(doc["spaced"].AsString().value_or(""), "hello world");
    EXPECT_EQ(doc["path"].AsString().value_or(""), "C:\\dir\\file");
    EXPECT_EQ(doc["tick"].AsString().value_or(""), "a`b");
    EXPECT_EQ(doc["color"].GetType(), Type::String);
}

TEST(Parser, HashEndsBareString) {
    Document doc = MustParse("a b#comment\nc d");
    EXPECT_EQ(doc["a"].AsString().value_or(""), "b");
    EXPECT_EQ(doc["c"].AsString().value_or(""), "d");
}

TEST(Parser, CrLf) {
    Document doc = MustParse("a 1\r\nb {\r\n  c 2\r\n}\r\n");
    EXPECT_EQ(doc["b"]["c"].As<int>().value_or(0), 2);
}

TEST(Parser, Bom) {
    Document doc = MustParse("\xEF\xBB\xBF"
                             "a 1");
    EXPECT_EQ(doc["a"].As<int>().value_or(0), 1);
}

TEST(Parser, Utf8) {
    Document doc = MustParse("name Гром\nemoji \"😀\"");
    EXPECT_EQ(doc["name"].AsString().value_or(""), "Гром");
    EXPECT_EQ(doc["emoji"].AsString().value_or(""), "😀");

    Error error = MustFail("a 1\nb \xFF");
    EXPECT_EQ(error.Code, ErrorCode::InvalidUtf8);
    EXPECT_EQ(error.Pos.Line, 2u);
    EXPECT_EQ(error.Pos.Column, 3u);

    EXPECT_EQ(MustFail("a \xC0\x80").Code, ErrorCode::InvalidUtf8);         // overlong
    EXPECT_EQ(MustFail("a \xED\xA0\x80").Code, ErrorCode::InvalidUtf8);     // surrogate
    EXPECT_EQ(MustFail("a \xF4\x90\x80\x80").Code, ErrorCode::InvalidUtf8); // > U+10FFFF
    EXPECT_EQ(MustFail("a \xE4\xBD").Code, ErrorCode::InvalidUtf8);         // truncated

    ParseOptions noCheck;
    noCheck.ValidateUtf8 = false;
    EXPECT_TRUE(Parse("a \xFF", noCheck).has_value());
}

TEST(Parser, Errors) {
    struct Case {
        const char *Source;
        ErrorCode   Code;
        u32         Line;
        u32         Column;
    };

    const Case cases[] = {
        {"a", ErrorCode::ExpectedValue, 1, 2},
        {"a }", ErrorCode::ExpectedValue, 1, 3},
        {"}", ErrorCode::UnexpectedCharacter, 1, 1},
        {"a {\n b 1", ErrorCode::UnclosedObject, 1, 3},
        {"a [1 2", ErrorCode::UnclosedList, 1, 3},
        {"a [1 }", ErrorCode::UnexpectedCharacter, 1, 6},
        {"a \"abc", ErrorCode::UnterminatedString, 1, 3},
        {"a \"abc\nd\"", ErrorCode::UnterminatedString, 1, 3},
        {"a `abc", ErrorCode::UnterminatedString, 1, 3},
        {"123 a", ErrorCode::InvalidKey, 1, 1},
        {"a.b 1", ErrorCode::InvalidKey, 1, 1},
        {"a 1\n  a 2", ErrorCode::DuplicateKey, 2, 3},
        {"o { x 1 \"x\" 2 }", ErrorCode::DuplicateKey, 1, 9},
        {"[1]", ErrorCode::UnexpectedCharacter, 1, 1},
        {"{ a 1 }", ErrorCode::UnexpectedCharacter, 1, 1},
    };

    for (const Case &c : cases) {
        Error error = MustFail(c.Source);
        EXPECT_EQ(error.Code, c.Code) << c.Source;
        EXPECT_EQ(error.Pos.Line, c.Line) << c.Source;
        EXPECT_EQ(error.Pos.Column, c.Column) << c.Source;
    }
}

TEST(Parser, DuplicateKeysInLargeObject) {
    std::string source = "o {\n";
    for (int i = 0; i < 100; ++i) {
        source += std::format("  key{} {}\n", i, i);
    }
    Document doc = MustParse(source + "}");
    EXPECT_EQ(doc["o"].Size(), 100u);
    EXPECT_EQ(doc["o"]["key99"].As<int>().value_or(0), 99);

    source += "  key42 1\n  key7 2\n}";
    Error error = MustFail(source);
    EXPECT_EQ(error.Code, ErrorCode::DuplicateKey);
    EXPECT_EQ(error.Pos.Line, 102u); // first duplicate in the file
}

TEST(Parser, MaxDepth) {
    std::string deep;
    for (int i = 0; i < 10; ++i) {
        deep += "a {";
    }
    deep += std::string(10, '}');

    ParseOptions options;
    options.MaxDepth = 10;
    EXPECT_TRUE(Parse(deep, options).has_value());
    options.MaxDepth = 9;
    EXPECT_EQ(Parse(deep, options).error().Code, ErrorCode::NestingTooDeep);

    std::string lists = "a " + std::string(1000, '[') + std::string(1000, ']');
    EXPECT_EQ(MustFail(lists).Code, ErrorCode::NestingTooDeep);
}

TEST(Parser, LargeListsSpillScratch) {
    std::string source = "list [";
    for (int i = 0; i < 1000; ++i) {
        source += std::format(" {{ i {} }}", i);
    }
    source += " ]";
    Document doc = MustParse(source);
    ASSERT_EQ(doc["list"].Size(), 1000u);
    EXPECT_EQ(doc["list"][999]["i"].As<int>().value_or(0), 999);
}

TEST(Parser, SourceIsCopiedByDefault) {
    std::string source = "name hero\ntitle \"The Hero\"";
    Document    doc = MustParse(source);
    source.assign(source.size(), 'X');
    EXPECT_EQ(doc["name"].AsString().value_or(""), "hero");
    EXPECT_EQ(doc["title"].AsString().value_or(""), "The Hero");
}

TEST(Parser, ZeroCopy) {
    std::string  source = "name hero";
    ParseOptions options;
    options.CopySource = false;
    Document doc = MustParse(source, options);

    std::string_view name = doc["name"].AsString().value_or("");
    EXPECT_EQ(name, "hero");
    EXPECT_GE(name.data(), source.data());
    EXPECT_LT(name.data(), source.data() + source.size());
}

TEST(Parser, ValuesSurviveDocumentMove) {
    Document doc = MustParse("a { b 1 }");
    Value    b = doc["a"]["b"];
    Document moved = std::move(doc);
    EXPECT_EQ(b.As<int>().value_or(0), 1);
    EXPECT_FALSE(doc.Root().IsValid()); // NOLINT(bugprone-use-after-move)
}

TEST(Parser, ErrorFormatting) {
    Error error = MustFail("a {\n  b 1\n  b 2\n}");
    EXPECT_EQ(std::format("{}", error), "3:3: duplicate key");
}

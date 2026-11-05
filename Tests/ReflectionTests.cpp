#include "TestUtils.h"

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace MDL; // NOLINT(google-build-using-namespace)
using MDL::Tests::MustParse;
using MDL::Tests::ReadAsset;

namespace Game {

enum class BlendMode : u8 {
    Opaque,
    AlphaBlend,
};

enum class Cull : u8 {
    None,
    Back,
    Front,
};

enum class Polygon : u8 {
    Fill,
    Line,
};

struct ShaderStage {
    std::string Src;
    std::string Entry;
};

struct ShaderStages {
    ShaderStage Vertex;
    ShaderStage Fragment;
};

struct PipelineState {
    bool      DepthTest = false;
    bool      DepthWrite = false;
    BlendMode Blend = BlendMode::AlphaBlend;
    Cull      CullMode = Cull::None;
    Polygon   PolygonMode = Polygon::Line;
};

struct ShaderAsset {
    std::string              Shader;
    ShaderStages             Stages;
    PipelineState            Pipeline;
    std::vector<std::string> SomeList;
    std::string              StringWithSpaces;
    std::string              BlackColor;
    std::string              WindowsPath;
    std::string              Hello2;
};

struct Region {
    std::string Name;
    i32         X = 0;
    i32         Y = 0;
    i32         Width = 0;
    i32         Height = 0;
};

struct Animation {
    std::string              Name;
    u32                      Delay = 0;
    std::vector<std::string> Frames;
};

struct Atlas {
    std::string            Texture;
    std::vector<Region>    Regions;
    std::vector<Animation> Animations;
};

struct Character {
    std::string                Name;
    i32                        Hp = 0;
    f32                        Speed = 0;
    bool                       IsBoss = true;
    std::string                FactionName;
    std::vector<std::string>   Tags;
    std::optional<std::string> Title;
};

// not an aggregate: needs a Serde specialization
class Vec3 {
  public:
    Vec3() = default;
    Vec3(f32 x, f32 y, f32 z) : m_X(x), m_Y(y), m_Z(z) {}

    f32 X() const { return m_X; }
    f32 Y() const { return m_Y; }
    f32 Z() const { return m_Z; }

  private:
    f32 m_X = 0;
    f32 m_Y = 0;
    f32 m_Z = 0;
};

struct Transform {
    Vec3                          Position;
    Vec3                          Scale {1, 1, 1};
    std::array<f32, 4>            Rotation {0, 0, 0, 1};
    std::map<std::string, i32>    Layers;
    std::optional<i32>            Parent;
    std::vector<std::vector<i32>> Grid;
};

struct AsIsNames {
    i32 CamelCase = 0;
    i32 snake_case = 0;
};

} // namespace Game

template <>
struct MDL::Serde<Game::Vec3> {
    static constexpr bool Flat = false;

    static std::expected<void, Error> Read(Value value, Game::Vec3 &out, const ReadOptions &) {
        if (!value.IsList() || value.Size() != 3) {
            return std::unexpected(value.MakeError(value.IsList() ? ErrorCode::SizeMismatch : ErrorCode::TypeMismatch));
        }
        std::array<f32, 3> v {};
        for (usize i = 0; i < 3; ++i) {
            auto component = value[i].As<f32>();
            if (!component) {
                return std::unexpected(component.error());
            }
            v[i] = *component;
        }
        out = Game::Vec3(v[0], v[1], v[2]);
        return {};
    }

    static void Write(Writer &w, const Game::Vec3 &v) {
        w.BeginList(Layout::Inline).Item(v.X()).Item(v.Y()).Item(v.Z()).EndList();
    }
};

template <>
struct MDL::Reflect<Game::AsIsNames> {
    static constexpr Naming Names = Naming::AsIs;
};


TEST(Reflection, FieldNames) {
    constexpr auto &keys = Detail::FieldKeys<Game::Character>;
    static_assert(keys.size() == 7);
    static_assert(keys[0] == "name");
    static_assert(keys[3] == "is_boss");
    static_assert(keys[4] == "faction_name");

    static_assert(Detail::FieldKeys<Game::AsIsNames>[0] == "CamelCase");
    static_assert(Detail::FieldKeys<Game::ShaderAsset>[7] == "hello2");
    static_assert(EnumToName(Game::BlendMode::AlphaBlend) == "alpha_blend");
}

TEST(Reflection, ReadShader) {
    auto shader = Load<Game::ShaderAsset>(ReadAsset("shader.mdl"));
    ASSERT_TRUE(shader.has_value()) << std::format("{}", shader.error());

    EXPECT_EQ(shader->Shader, "pbr");
    EXPECT_EQ(shader->Stages.Vertex.Src, "shaders/pbr.slang");
    EXPECT_EQ(shader->Stages.Fragment.Entry, "fs_main");
    EXPECT_TRUE(shader->Pipeline.DepthTest);
    EXPECT_EQ(shader->Pipeline.Blend, Game::BlendMode::Opaque);
    EXPECT_EQ(shader->Pipeline.CullMode, Game::Cull::Back);
    EXPECT_EQ(shader->Pipeline.PolygonMode, Game::Polygon::Fill);
    EXPECT_EQ(shader->SomeList, (std::vector<std::string> {"Monday", "Tuesday"}));
    EXPECT_EQ(shader->BlackColor, "#FFFFFF");
    EXPECT_EQ(shader->WindowsPath, "C:\\Projects\\Mantle");
    EXPECT_EQ(shader->Hello2, "Hello ` World!");
}

TEST(Reflection, ReadAtlas) {
    auto atlas = Load<Game::Atlas>(ReadAsset("atlas.mdl"));
    ASSERT_TRUE(atlas.has_value()) << std::format("{}", atlas.error());

    EXPECT_EQ(atlas->Texture, "Images/Atlas");
    ASSERT_EQ(atlas->Regions.size(), 5u);
    EXPECT_EQ(atlas->Regions[4].Name, "bat-3");
    EXPECT_EQ(atlas->Regions[4].X, 40);
    ASSERT_EQ(atlas->Animations.size(), 2u);
    EXPECT_EQ(atlas->Animations[1].Delay, 200u);
    EXPECT_EQ(atlas->Animations[1].Frames.size(), 4u);
}

TEST(Reflection, ReadCharacter) {
    auto hero = Load<Game::Character>(ReadAsset("character.mdl"));
    ASSERT_TRUE(hero.has_value()) << std::format("{}", hero.error());

    EXPECT_EQ(hero->Name, "Grom");
    EXPECT_EQ(hero->Hp, 100);
    EXPECT_FLOAT_EQ(hero->Speed, 5.5f);
    EXPECT_FALSE(hero->IsBoss);
    EXPECT_EQ(hero->FactionName, "Orcs");
    EXPECT_EQ(hero->Tags, (std::vector<std::string> {"sword", "shield"}));
    EXPECT_FALSE(hero->Title.has_value());
}

TEST(Reflection, WriteAtlas) {
    auto atlas = Load<Game::Atlas>(ReadAsset("atlas.mdl"));
    ASSERT_TRUE(atlas.has_value());

    auto text = WriteToString(*atlas);
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

TEST(Reflection, WriteCharacter) {
    Game::Character hero {
        .Name = "Grom Hellscream",
        .Hp = 100,
        .Speed = 5.5f,
        .IsBoss = false,
        .FactionName = "Orcs",
        .Tags = {"sword", "shield"},
    };

    auto text = WriteToString(hero);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text,
              "name \"Grom Hellscream\"\n"
              "hp 100\n"
              "speed 5.5\n"
              "is_boss false\n"
              "faction_name Orcs\n"
              "tags [sword shield]\n");

    hero.Title = "Warchief";
    text = WriteToString(hero);
    ASSERT_TRUE(text.has_value());
    EXPECT_TRUE(text->ends_with("title Warchief\n"));
}

TEST(Reflection, RoundTrip) {
    Game::Transform transform {
        .Position = {1.5f, -2, 3},
        .Layers = {{"ui", 3}, {"world", 1}},
        .Parent = 7,
        .Grid = {{1, 2}, {3}},
    };

    auto text = WriteToString(transform);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text,
              "position [1.5 -2.0 3.0]\n"
              "scale [1.0 1.0 1.0]\n"
              "rotation [0.0 0.0 0.0 1.0]\n"
              "layers {\n"
              "  ui 3\n"
              "  world 1\n"
              "}\n"
              "parent 7\n"
              "grid [\n"
              "  [1 2]\n"
              "  [3]\n"
              "]\n");

    auto back = Load<Game::Transform>(*text);
    ASSERT_TRUE(back.has_value()) << std::format("{}", back.error());
    EXPECT_FLOAT_EQ(back->Position.X(), 1.5f);
    EXPECT_FLOAT_EQ(back->Position.Y(), -2.0f);
    EXPECT_FLOAT_EQ(back->Scale.Z(), 1.0f);
    EXPECT_EQ(back->Layers.at("ui"), 3);
    EXPECT_EQ(back->Parent, 7);
    EXPECT_EQ(back->Grid, transform.Grid);
}

TEST(Reflection, UnknownKeys) {
    auto result = Load<Game::Character>("name a\n  hpp 10");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().Code, ErrorCode::UnknownKey);
    EXPECT_EQ(result.error().Pos.Line, 2u);
    EXPECT_EQ(result.error().Pos.Column, 3u);

    ReadOptions lenient;
    lenient.Unknown = UnknownKeys::Ignore;
    auto ignored = Load<Game::Character>("name a hpp 10", {}, lenient);
    ASSERT_TRUE(ignored.has_value());
    EXPECT_EQ(ignored->Name, "a");
}

TEST(Reflection, MissingKeys) {
    auto defaults = Load<Game::Character>("name a");
    ASSERT_TRUE(defaults.has_value());
    EXPECT_TRUE(defaults->IsBoss); // default member initializer kept

    ReadOptions strict;
    strict.Missing = MissingKeys::Error;
    auto result = Load<Game::Character>("name a hp 1 speed 1 is_boss true faction_name x", {}, strict);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().Code, ErrorCode::MissingKey);
    EXPECT_EQ(result.error().Context, "tags");
    EXPECT_EQ(std::format("{}", result.error()), "1:1: missing key 'tags'");

    // optional fields may be missing even in strict mode
    auto ok = Load<Game::Character>("name a hp 1 speed 1 is_boss true faction_name x tags []", {}, strict);
    EXPECT_TRUE(ok.has_value());
}

TEST(Reflection, NestedErrorsPointAtTheValue) {
    auto result = Load<Game::Atlas>("texture t\nregions [\n  { name a x 1 }\n  { name b x oops }\n]");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().Code, ErrorCode::TypeMismatch);
    EXPECT_EQ(result.error().Pos.Line, 4u);
    EXPECT_EQ(result.error().Pos.Column, 14u);

    auto badEnum = Load<Game::PipelineState>("blend glass");
    ASSERT_FALSE(badEnum.has_value());
    EXPECT_EQ(badEnum.error().Code, ErrorCode::InvalidEnum);

    auto badArray = Load<Game::Transform>("rotation [1 2 3]");
    ASSERT_FALSE(badArray.has_value());
    EXPECT_EQ(badArray.error().Code, ErrorCode::SizeMismatch);

    auto badVec = Load<Game::Transform>("position [1 2]");
    ASSERT_FALSE(badVec.has_value());
    EXPECT_EQ(badVec.error().Code, ErrorCode::SizeMismatch);
}

TEST(Reflection, ReadFromDocumentSubtree) {
    Document doc = MustParse(ReadAsset("shader.mdl"));

    auto pipeline = Read<Game::PipelineState>(doc["pipeline"]);
    ASSERT_TRUE(pipeline.has_value());
    EXPECT_TRUE(pipeline->DepthWrite);

    Game::ShaderStage stage;
    ASSERT_TRUE(ReadInto(doc["stages"]["vertex"], stage).has_value());
    EXPECT_EQ(stage.Entry, "vs_main");

    auto missing = Read<Game::PipelineState>(doc["nope"]);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().Code, ErrorCode::MissingKey);
}

TEST(Reflection, ViewsAndValues) {
    struct Header {
        std::string_view Name;
        Value            Extra;
    };

    Document doc = MustParse("name hero extra { anything [1 2 3] }");
    auto     header = Read<Header>(doc.Root());
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->Name, "hero");
    EXPECT_EQ(header->Extra["anything"].Size(), 3u);
}

TEST(Reflection, MapsAndAsIsNames) {
    auto names = Load<Game::AsIsNames>("CamelCase 1 snake_case 2");
    ASSERT_TRUE(names.has_value());
    EXPECT_EQ(names->CamelCase, 1);
    EXPECT_EQ(names->snake_case, 2);

    auto map = Load<std::unordered_map<std::string, std::vector<i32>>>("a [1 2] b []");
    ASSERT_TRUE(map.has_value());
    EXPECT_EQ(map->at("a").size(), 2u);
    EXPECT_TRUE(map->at("b").empty());
}

TEST(Reflection, StreamingWithStructs) {
    std::string text;
    Writer      w(Output::ToString(text));
    w.Field("name", "atlas");
    w.Field("region", Game::Region {.Name = "a", .X = 1, .Y = 2, .Width = 3, .Height = 4});
    w.BeginList("more").Item(Game::Region {.Name = "b"}).EndList();
    ASSERT_TRUE(w.Finish().has_value());
    EXPECT_EQ(text,
              "name atlas\n"
              "region {\n"
              "  name a\n"
              "  x 1\n"
              "  y 2\n"
              "  width 3\n"
              "  height 4\n"
              "}\n"
              "more [\n"
              "  { name b x 0 y 0 width 0 height 0 }\n"
              "]\n");
}

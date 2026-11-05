# MDL.hpp

C++23 parser and writer for [MDL](https://github.com/koftamainee/MDL) (Mantle Data Language).

- DOM access: `doc["stages"]["vertex"]["src"].AsString()`
- Compile-time reflection of plain structs and enums, no macros: `MDL::Load<Shader>(text)`
- Writer with a pluggable output (VFS, string, callback)
- Every allocation goes through an `MDL::Allocator` you control, plus an optional user buffer
- No exceptions, no RTTI, errors are `std::expected<T, MDL::Error>` with line:column
- ~8 µs / ~800 MB/s for a 300-line config (clang, Release)

## Integration

Requires CMake 3.25+ and a C++23 compiler (clang and MSVC first, gcc works too).

```cmake
# git submodule
add_subdirectory(ThirdParty/MDL)

# or FetchContent
include(FetchContent)
FetchContent_Declare(MDL GIT_REPOSITORY https://github.com/koftamainee/MDL.hpp.git GIT_TAG main)
FetchContent_MakeAvailable(MDL)

target_link_libraries(Game PRIVATE MDL::MDL)
```

Options (set before `add_subdirectory` / `FetchContent_MakeAvailable`):

| Option | Default | |
|---|---|---|
| `MDL_BUILD_TESTS` | ON when top level | GoogleTest tests |
| `MDL_BUILD_BENCHMARKS` | OFF | `Bench/` |
| `MDL_WARNINGS_AS_ERRORS` | ON when top level | |
| `MDL_SNAKE_CASE_NAMES` | ON | reflection maps `DepthTest` -> `depth_test` |
| `MDL_MAX_DEPTH` | 128 | nesting limit for parser and writer |
| `MDL_ENUM_MIN` / `MDL_ENUM_MAX` | -16 / 127 | enum values scanned by enum reflection |
| `MDL_ENABLE_ASSERTS` | ON in Debug | internal checks |
| `MDL_GOOGLETEST_TAG` | v1.17.0 | |

## DOM

```cpp
#include <MDL/MDL.h>

auto doc = MDL::Parse(text);
if (!doc) {
    std::println("{}", doc.error()); // "12:5: duplicate key"
    return;
}

std::string_view src = (*doc)["stages"]["vertex"]["src"].AsString().value_or("");
MDL::i32 hp = (*doc)["hp"].ValueOr(100);
auto speed = (*doc)["speed"].As<MDL::f32>(); // std::expected<f32, Error>

for (auto [key, value] : (*doc)["pipeline"].Members()) { ... }
for (MDL::Value item : (*doc)["tags"].Items()) { ... }
```

Lookups never fail loudly: a missing key gives an invalid `Value`, and `As*()` returns
`MissingKey` with the position of the last existing parent.

`As<T>` supports `bool`, all integer types, `f32`, `f64`, enums and `std::string_view`.
Integers are range checked; floats read as integers are truncated toward zero
(`3.9` -> `3`). Every scalar can be read as a string (`AsString()`).

## Reflection

Aggregates (structs without constructors / bases) are reflected at compile time.

```cpp
enum class Cull : MDL::u8 { None, Back, Front };

struct Pipeline {
    bool DepthTest = true;       // key: depth_test
    Cull CullMode = Cull::Back;  // key: cull_mode, value: none / back / front
    std::optional<float> LineWidth;
};

struct Shader {
    std::string              Name;
    Pipeline                 Pipeline;
    std::vector<std::string> Tags;
    std::array<float, 4>     Color;
    std::map<std::string, int> Defines;
};

auto shader = MDL::Load<Shader>(text);                // parse + read
auto pipe   = MDL::Read<Pipeline>((*doc)["pipeline"]); // read a subtree
auto out    = MDL::WriteToString(*shader);
```

Supported: arithmetic types, enums, `std::basic_string` (any allocator, e.g. `std::pmr::string`),
`std::string_view` and `MDL::Value` (point into the document), `std::optional`, vector-like
containers, `std::array`, map-like containers with string keys, nested aggregates.

`ReadOptions`:
- `Unknown = UnknownKeys::Error` (default) - a key with no matching field is an error, catches typos
- `Missing = MissingKeys::KeepDefault` (default) - absent fields keep their initializers;
  `MissingKeys::Error` requires every non-optional field

Per-type settings:

```cpp
template <>
struct MDL::Reflect<MyType> {
    static constexpr MDL::Naming Names = MDL::Naming::AsIs;
};

template <>
struct MDL::Reflect<BigEnum> {
    static constexpr MDL::i64 EnumMax = 512;
};
```

Custom types (non-aggregates, math types, ...):

```cpp
template <>
struct MDL::Serde<glm::vec3> {
    static std::expected<void, MDL::Error> Read(MDL::Value v, glm::vec3 &out, const MDL::ReadOptions &) {
        if (!v.IsList() || v.Size() != 3) {
            return std::unexpected(v.MakeError(MDL::ErrorCode::SizeMismatch));
        }
        out = {v[0].ValueOr(0.0f), v[1].ValueOr(0.0f), v[2].ValueOr(0.0f)};
        return {};
    }

    static void Write(MDL::Writer &w, const glm::vec3 &v) {
        w.BeginList(MDL::Layout::Inline).Item(v.x).Item(v.y).Item(v.z).EndList();
    }
};
```

Limits: up to 64 fields per struct (`Tools/GenerateTie.py <N>` to raise), no C arrays or
bit-fields in reflected structs, unscoped enums need a fixed underlying type.

## Writing

```cpp
std::string text;
MDL::Writer w(MDL::Output::ToString(text));
w.Field("shader", "pbr");
w.BeginObject("pipeline").Field("depth_test", true).Field("cull_mode", Cull::Back).EndObject();
w.BeginList("tags").Item("sword").Item("shield").EndList();
auto status = w.Finish();
```

```
shader pbr
pipeline {
  depth_test true
  cull_mode back
}
tags [sword shield]
```

The writer buffers 4 KB and hands chunks to an `Output`, so it can go anywhere:

```cpp
auto sink = [&](std::string_view chunk) { return vfsFile.Write(chunk); }; // bool or void
MDL::Write(shader, MDL::Output::From(sink));
```

Strings are written bare when possible, as `` `raw` `` when they contain backslashes, and quoted
otherwise. Strings that look like numbers or bools are quoted so they read back as strings.

## Memory

```cpp
MDL::ParseOptions options;
options.Alloc = MDL::Allocator::From(myAllocator); // Alloc/Free, Push (arena) or std::pmr::memory_resource
options.Buffer = stackBuffer;                      // used before the allocator
options.CopySource = false;                        // strings point into your text (must outlive the doc)
```

A `Document` keeps all nodes in one arena: a typical asset file is one allocation, and zero
with a large enough `Buffer`. The parser keeps its scratch stacks on the C++ stack and only spills
to the allocator for very long lists/objects. `Document` is move-only, and `Value`s stay valid
when it is moved.

## Syntax details

This implementation follows `grammar.js` from the MDL repo, plus these decisions:

- Duplicate keys in one object are an error (compared after unescaping, `a` and `"a"` clash).
- `"..."` supports the C escapes: `\a \b \f \n \r \t \v \\ \' \" \?`, octal `\0`-`\377`,
  `\xH`/`\xHH` (at most two digits), plus `\uXXXX` and `\UXXXXXXXX`. Unknown escapes such as `\q`
  are kept as is. Malformed `\x`, `\u`, surrogates and code points above U+10FFFF are errors.
- `` `...` `` raw strings: only `` \` `` is an escape. A raw string cannot end with a backslash,
  use `"C:\\Projects\\"` for that (the writer does it automatically).
- Bare strings: `\` before one of ``# { } [ ] " ` `` space or tab escapes it (`\#FFFFFF`,
  `hello\ world`). Any other backslash is literal, so `C:\dir\file` works bare.
- Bare `true`/`false` are bools, bare `-?\d+(\.\d+)?([eE][+-]?\d+)?` are numbers.
  Quoted and raw strings are always strings: `"123"` cannot be read as a number.
- Input must be UTF-8 (validated, can be disabled). A UTF-8 BOM is skipped.
- Columns in error positions count bytes.

## Building

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build

cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DMDL_BUILD_BENCHMARKS=ON
cmake --build build-release && ./build-release/Bench/MDLBench
```

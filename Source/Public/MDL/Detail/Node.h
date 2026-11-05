#pragma once

#include <MDL/Types.h>


namespace MDL {

enum class Type : u8 {
    None, // missing value (failed lookup)
    Object,
    List,
    String,
    Number,
    Bool,
};

// How a scalar was written in the source.
enum class StringStyle : u8 {
    Bare,   // hello
    Quoted, // "hello"
    Raw,    // `hello`
};

namespace Detail {

enum NodeFlag : u8 {
    NodeFlagStyleMask = 0x3,  // StringStyle
    NodeFlagFloat = 1u << 2,  // number has a fraction or an exponent
    NodeFlagTrue = 1u << 3,   // bool value
    NodeFlagDecoded = 1u << 4 // text was unescaped into the document arena
};

// Scalars: Data -> const char[Size]
// Object:  Data -> const Member[Size]
// List:    Data -> const Node[Size]
struct Node final {
    const void *Data = nullptr;
    u32         Size = 0;
    u32         Line = 0;
    u32         Column = 0;
    Type        Kind = Type::None;
    u8          Flags = 0;
};

struct Member final {
    Node        Val;
    const char *Key = nullptr;
    u32         KeySize = 0;
    u32         KeyLine = 0;
    u32         KeyColumn = 0;
};

static_assert(sizeof(Node) == 24);

} // namespace Detail
} // namespace MDL

#pragma once

#include <cstddef>
#include <cstdint>


namespace MDL {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;

static_assert(sizeof(f32) == 4);
static_assert(sizeof(f64) == 8);

// NOTE(koftamainee): 1-based. Column counts bytes from the start of the line.
// Line == 0 means "no position".
struct Position final {
    u32 Line = 0;
    u32 Column = 0;
};

} // namespace MDL

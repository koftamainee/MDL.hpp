#pragma once

#include <MDL/Allocator.h>
#include <MDL/Types.h>

#include <span>


namespace MDL::Detail {

// Growing bump allocator. Starts from an optional user buffer, then requests
// blocks from the Allocator. Everything is freed at once.
class BlockArena final {
  public:
    BlockArena() = default;
    ~BlockArena();

    BlockArena(const BlockArena &) = delete;
    BlockArena(BlockArena &&other) noexcept;
    BlockArena &operator=(const BlockArena &) = delete;
    BlockArena &operator=(BlockArena &&other) noexcept;

    void Init(Allocator alloc, std::span<std::byte> initial, usize firstBlockSize);
    void Release();

    // nullptr when out of memory
    [[nodiscard]] void *Push(usize size, usize align);

    // Gives back the tail of the most recent allocation.
    void ShrinkLast(void *ptr, usize oldSize, usize newSize);

    template <class T>
    [[nodiscard]] T *Push(usize count = 1) {
        return static_cast<T *>(Push(sizeof(T) * count, alignof(T)));
    }

    usize BytesUsed() const { return m_Used; }
    usize BytesReserved() const { return m_Reserved; }

  private:
    struct Block final {
        Block *Next;
        usize  Size;
    };

    bool Grow(usize minSize);

    Allocator  m_Alloc {};
    Block     *m_Blocks = nullptr;
    std::byte *m_Cur = nullptr;
    std::byte *m_End = nullptr;
    usize      m_NextBlockSize = 0;
    usize      m_Used = 0;
    usize      m_Reserved = 0;
};

} // namespace MDL::Detail

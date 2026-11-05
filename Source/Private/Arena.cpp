#include <MDL/Detail/Arena.h>

#include <MDL/Assert.h>

#include <algorithm>
#include <cstdint>
#include <utility>


namespace MDL::Detail {
namespace {

constexpr usize MinBlockSize = 1024;

std::byte *AlignUp(std::byte *ptr, usize align) {
    auto value = reinterpret_cast<std::uintptr_t>(ptr);
    value = (value + (align - 1)) & ~(static_cast<std::uintptr_t>(align) - 1);
    return reinterpret_cast<std::byte *>(value);
}

} // namespace


BlockArena::~BlockArena() { Release(); }

BlockArena::BlockArena(BlockArena &&other) noexcept { *this = std::move(other); }

BlockArena &BlockArena::operator=(BlockArena &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    Release();

    m_Alloc = other.m_Alloc;
    m_Blocks = std::exchange(other.m_Blocks, nullptr);
    m_Cur = std::exchange(other.m_Cur, nullptr);
    m_End = std::exchange(other.m_End, nullptr);
    m_NextBlockSize = std::exchange(other.m_NextBlockSize, 0);
    m_Used = std::exchange(other.m_Used, 0);
    m_Reserved = std::exchange(other.m_Reserved, 0);
    return *this;
}

void BlockArena::Init(Allocator alloc, std::span<std::byte> initial, usize firstBlockSize) {
    MDL_CHECK(alloc.AllocFn != nullptr && alloc.FreeFn != nullptr);

    Release();
    m_Alloc = alloc;
    m_Cur = initial.data();
    m_End = initial.data() + initial.size();
    m_NextBlockSize = std::max(firstBlockSize, MinBlockSize);
    m_Reserved = initial.size();
}

void BlockArena::Release() {
    Block *block = m_Blocks;
    while (block != nullptr) {
        Block *next = block->Next;
        m_Alloc.Free(block, block->Size, alignof(Block));
        block = next;
    }

    m_Blocks = nullptr;
    m_Cur = nullptr;
    m_End = nullptr;
    m_Used = 0;
    m_Reserved = 0;
}

void *BlockArena::Push(usize size, usize align) {
    std::byte *ptr = AlignUp(m_Cur, align);
    if (m_Cur == nullptr || ptr + size > m_End) {
        if (!Grow(size + align)) {
            return nullptr;
        }
        ptr = AlignUp(m_Cur, align);
    }

    m_Used += static_cast<usize>(ptr + size - m_Cur);
    m_Cur = ptr + size;
    return ptr;
}

void BlockArena::ShrinkLast(void *ptr, usize oldSize, usize newSize) {
    auto *bytes = static_cast<std::byte *>(ptr);
    if (bytes + oldSize == m_Cur && newSize <= oldSize) {
        m_Cur = bytes + newSize;
        m_Used -= oldSize - newSize;
    }
}

bool BlockArena::Grow(usize minSize) {
    usize payload = std::max(m_NextBlockSize, minSize);
    usize total = sizeof(Block) + payload;

    void *memory = m_Alloc.Alloc(total, alignof(Block));
    if (memory == nullptr) {
        return false;
    }

    auto *block = static_cast<Block *>(memory);
    block->Next = m_Blocks;
    block->Size = total;
    m_Blocks = block;

    m_Cur = reinterpret_cast<std::byte *>(block + 1);
    m_End = m_Cur + payload;
    m_NextBlockSize = payload * 2;
    m_Reserved += total;
    return true;
}

} // namespace MDL::Detail

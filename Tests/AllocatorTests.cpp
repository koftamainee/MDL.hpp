#include "TestUtils.h"

#include <array>
#include <cstdlib>
#include <memory_resource>

using namespace MDL; // NOLINT(google-build-using-namespace)
using MDL::Tests::ReadAsset;

namespace {

// General purpose allocator with the same shape as Mantle::GPAllocator.
class CountingAllocator {
  public:
    void *Alloc(usize size, usize align) {
        ++Allocs;
        Bytes += size;
        EXPECT_LE(align, alignof(std::max_align_t));
        return std::malloc(size); // NOLINT
    }

    void Free(void *ptr) {
        ++Frees;
        std::free(ptr); // NOLINT
    }

    int   Allocs = 0;
    int   Frees = 0;
    usize Bytes = 0;
};

// Bump allocator with the same shape as Mantle::Arena.
class BumpArena {
  public:
    void *Push(usize size, usize align) {
        usize offset = (m_Offset + align - 1) & ~(align - 1);
        if (offset + size > m_Memory.size()) {
            return nullptr;
        }
        m_Offset = offset + size;
        return m_Memory.data() + offset;
    }

    usize Offset() const { return m_Offset; }

  private:
    alignas(std::max_align_t) std::array<std::byte, 64 * 1024> m_Memory {};
    usize m_Offset = 0;
};

struct FailingAllocator {
    void *Alloc(usize, usize) { return nullptr; }
    void  Free(void *) {}
};

} // namespace


TEST(Allocator, CustomAllocatorIsUsedAndEverythingIsFreed) {
    CountingAllocator counter;
    {
        ParseOptions options;
        options.Alloc = Allocator::From(counter);
        auto doc = Parse(ReadAsset("shader.mdl"), options);
        ASSERT_TRUE(doc.has_value());
        EXPECT_GT(counter.Allocs, 0);
        EXPECT_EQ((*doc)["shader"].AsString().value_or(""), "pbr");
    }
    EXPECT_EQ(counter.Allocs, counter.Frees);
}

TEST(Allocator, SmallFileIsOneAllocation) {
    CountingAllocator counter;
    ParseOptions      options;
    options.Alloc = Allocator::From(counter);
    auto doc = Parse(ReadAsset("atlas.mdl"), options);
    ASSERT_TRUE(doc.has_value());
    EXPECT_EQ(counter.Allocs, 1);
}

TEST(Allocator, UserBufferMeansNoAllocations) {
    CountingAllocator counter;

    alignas(std::max_align_t) std::array<std::byte, 16 * 1024> buffer {};
    ParseOptions                                               options;
    options.Alloc = Allocator::From(counter);
    options.Buffer = buffer;

    auto doc = Parse(ReadAsset("shader.mdl"), options);
    ASSERT_TRUE(doc.has_value());
    EXPECT_EQ(counter.Allocs, 0);
    EXPECT_GT(doc->MemoryUsed(), 0u);
    EXPECT_LE(doc->MemoryUsed(), buffer.size());
    EXPECT_EQ((*doc)["stages"]["vertex"]["entry"].AsString().value_or(""), "vs_main");
}

TEST(Allocator, BufferOverflowFallsBackToAllocator) {
    CountingAllocator counter;

    std::array<std::byte, 64> buffer {};
    ParseOptions              options;
    options.Alloc = Allocator::From(counter);
    options.Buffer = buffer;

    auto doc = Parse(ReadAsset("atlas.mdl"), options);
    ASSERT_TRUE(doc.has_value());
    EXPECT_GT(counter.Allocs, 0);
    EXPECT_EQ((*doc)["regions"][4]["name"].AsString().value_or(""), "bat-3");
}

TEST(Allocator, ArenaStyleAllocator) {
    BumpArena    arena;
    ParseOptions options;
    options.Alloc = Allocator::From(arena);

    auto doc = Parse(ReadAsset("character.mdl"), options);
    ASSERT_TRUE(doc.has_value());
    EXPECT_GT(arena.Offset(), 0u);
    EXPECT_EQ((*doc)["faction_name"].AsString().value_or(""), "Orcs");
}

TEST(Allocator, PmrResource) {
    std::array<std::byte, 32 * 1024>    storage {};
    std::pmr::monotonic_buffer_resource resource(storage.data(), storage.size(), std::pmr::null_memory_resource());

    ParseOptions options;
    options.Alloc = Allocator::From(resource);
    auto doc = Parse(ReadAsset("atlas.mdl"), options);
    ASSERT_TRUE(doc.has_value());
    EXPECT_EQ((*doc)["animations"][0]["delay"].As<i32>().value_or(0), 200);
}

TEST(Allocator, OutOfMemory) {
    FailingAllocator failing;
    ParseOptions     options;
    options.Alloc = Allocator::From(failing);

    auto doc = Parse("a 1", options);
    ASSERT_FALSE(doc.has_value());
    EXPECT_EQ(doc.error().Code, ErrorCode::OutOfMemory);

    // scratch stacks spill to the allocator on big lists
    std::array<std::byte, 64 * 1024> buffer {};
    options.Buffer = buffer;
    std::string big = "l [";
    for (int i = 0; i < 1000; ++i) {
        big += " 1";
    }
    big += "]";
    auto spilled = Parse(big, options);
    ASSERT_FALSE(spilled.has_value());
    EXPECT_EQ(spilled.error().Code, ErrorCode::OutOfMemory);
}

TEST(Allocator, PmrStringsInReflection) {
    struct Config {
        std::pmr::string                   Name;
        std::pmr::vector<std::pmr::string> Tags;
    };

    std::array<std::byte, 4096>         storage {};
    std::pmr::monotonic_buffer_resource resource(storage.data(), storage.size());

    auto doc = Parse("name hero tags [a b]");
    ASSERT_TRUE(doc.has_value());

    Config config {.Name = std::pmr::string(&resource), .Tags = std::pmr::vector<std::pmr::string>(&resource)};
    ASSERT_TRUE(ReadInto(doc->Root(), config).has_value());
    EXPECT_EQ(config.Name, "hero");
    EXPECT_EQ(config.Tags.size(), 2u);
    EXPECT_EQ(config.Tags.get_allocator().resource(), &resource);
}

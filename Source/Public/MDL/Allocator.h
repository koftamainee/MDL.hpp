#pragma once

#include <MDL/Types.h>

#include <memory_resource>
#include <new>


namespace MDL {

// Type-erased allocator. Everything MDL allocates goes through one of these.
// The object behind User must outlive whatever was allocated with it.
struct Allocator final {
    void *User = nullptr;
    void *(*AllocFn)(void *user, usize size, usize align) = nullptr;
    void (*FreeFn)(void *user, void *ptr, usize size, usize align) = nullptr;


    [[nodiscard]] void *Alloc(usize size, usize align) const {
        return AllocFn(User, size, align);
    }

    void Free(void *ptr, usize size, usize align) const {
        if (ptr != nullptr) {
            FreeFn(User, ptr, size, align);
        }
    }


    // global operator new / delete (nothrow)
    static constexpr Allocator Default() {
        return Allocator {
            .User = nullptr,
            .AllocFn = [](void *, usize size, usize align) -> void * {
                return ::operator new(size, std::align_val_t {align}, std::nothrow);
            },
            .FreeFn = [](void *, void *ptr, usize, usize align) { ::operator delete(ptr, std::align_val_t {align}); },
        };
    }


    // Wraps any object that has one of:
    //   void *Alloc(usize size, usize align) + void Free(void *ptr)   (general purpose)
    //   void *Push(usize size, usize align)                           (arena, frees are no-ops)
    //   std::pmr::memory_resource
    template <class T>
    static Allocator From(T &impl) {
        Allocator result;
        result.User = &impl;

        if constexpr (std::is_base_of_v<std::pmr::memory_resource, T>) {
            result.AllocFn = [](void *user, usize size, usize align) -> void * {
                return static_cast<T *>(user)->allocate(size, align);
            };
            result.FreeFn = [](void *user, void *ptr, usize size, usize align) {
                static_cast<T *>(user)->deallocate(ptr, size, align);
            };
        } else if constexpr (requires(T &t, usize s) { t.Alloc(s, s); }) {
            result.AllocFn = [](void *user, usize size, usize align) -> void * {
                return static_cast<T *>(user)->Alloc(size, align);
            };
            result.FreeFn = [](void *user, void *ptr, usize, usize) {
                static_cast<T *>(user)->Free(ptr);
            };
        } else if constexpr (requires(T &t, usize s) { t.Push(s, s); }) {
            result.AllocFn = [](void *user, usize size, usize align) -> void * {
                return static_cast<T *>(user)->Push(size, align);
            };
            result.FreeFn = [](void *, void *, usize, usize) {};
        } else {
            static_assert(sizeof(T) == 0, "MDL::Allocator::From: unsupported allocator type");
        }

        return result;
    }
};

} // namespace MDL

#ifndef OBJECT_POOL_H
#define OBJECT_POOL_H

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

// M5 Section 1: fixed-capacity pool for any object type
// Storage is reserved once, objects are constructed only when acquired
template <typename T>
class ObjectPool {
    static_assert(std::is_nothrow_destructible<T>::value,
                  "Pooled objects must have non-throwing destructors");

    struct Slot {
        alignas(T) unsigned char storage[sizeof(T)];
    };

    struct State {
        explicit State(std::size_t capacity) : slots(capacity) {
            freeSlots.reserve(capacity);
            for (std::size_t i = capacity; i > 0; --i) {
                freeSlots.push_back(i - 1);
            }
        }

        std::vector<Slot> slots;
        std::vector<std::size_t> freeSlots;
    };

public:
    // Moving or destroying a handle automatically returns its slot
    // Shared state keeps the storage alive even if a handle outlives the pool
    struct Deleter {
        std::shared_ptr<State> state;
        std::size_t index = 0;

        void operator()(T* object) const noexcept {
            if (!object) return;
            object->~T();
            state->freeSlots.push_back(index);
        }
    };

    using Handle = std::unique_ptr<T, Deleter>;

    explicit ObjectPool(std::size_t capacity)
        : state(std::make_shared<State>(capacity)) {}

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    template <typename... Args>
    Handle acquire(Args&&... args) {
        if (state->freeSlots.empty()) return Handle(nullptr, Deleter{});

        std::size_t index = state->freeSlots.back();
        state->freeSlots.pop_back();
        try {
            T* object = ::new (static_cast<void*>(state->slots[index].storage))
                T(std::forward<Args>(args)...);
            return Handle(object, Deleter{state, index});
        } catch (...) {
            state->freeSlots.push_back(index);
            throw;
        }
    }

    std::size_t capacity() const { return state->slots.size(); }
    std::size_t available() const { return state->freeSlots.size(); }
    std::size_t inUse() const { return capacity() - available(); }

private:
    // Like the game update loop, this pool is intended for single threaded use.
    std::shared_ptr<State> state;
};

#endif

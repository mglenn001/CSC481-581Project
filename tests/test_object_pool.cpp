// M5 Section 1: headless tests for the reusable memory pool.
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "../src/ObjectPool.h"
#include "../src/GameObject.h"

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAIL: " #condition << " (line " << __LINE__ << ")\n"; \
    ++failures; } } while (0)

struct TestComponent : Component {
    int value = 42;
};

struct alignas(64) Tracked {
    static int alive;
    explicit Tracked(bool fail = false) {
        if (fail) throw std::runtime_error("constructor failed");
        ++alive;
    }
    ~Tracked() { --alive; }
};
int Tracked::alive = 0;

int main() {
    ObjectPool<GameObject> pool(2);
    CHECK(pool.capacity() == 2 && pool.available() == 2);
    auto first = pool.acquire("bullet", 1);
    auto second = pool.acquire("bullet", 2);
    CHECK(first && second && first.get() != second.get());
    CHECK(pool.inUse() == 2 && !pool.acquire("overflow"));
    first->addComponent<TestComponent>();
    auto* address = first.get();
    first.reset();
    CHECK(pool.available() == 1);
    auto reused = pool.acquire("enemy", 3);
    CHECK(reused.get() == address);
    CHECK(reused->getId() == 3 && reused->getType() == "enemy");
    CHECK(!reused->hasComponent<TestComponent>());

    // Same erase/remove pattern as the game's bullet lists.
    std::vector<ObjectPool<GameObject>::Handle> active;
    active.push_back(std::move(second));
    active.push_back(std::move(reused));
    active.erase(std::remove_if(active.begin(), active.end(),
        [](const auto& obj) { return obj->getId() == 2; }), active.end());
    CHECK(pool.inUse() == 1 && active.size() == 1);
    CHECK(active.front()->getId() == 3);
    active.clear();
    CHECK(pool.available() == 2);

    ObjectPool<Tracked> tracked(1);
    try {
        tracked.acquire(true);
        CHECK(false);
    } catch (const std::runtime_error&) {
        CHECK(tracked.available() == 1 && Tracked::alive == 0);
    }
    auto item = tracked.acquire();
    CHECK(item && reinterpret_cast<std::uintptr_t>(item.get()) % alignof(Tracked) == 0);
    CHECK(Tracked::alive == 1);
    item.reset();
    CHECK(Tracked::alive == 0 && tracked.available() == 1);

    // Handles keep reserved storage alive until their own destruction.
    auto survivor = [] {
        ObjectPool<Tracked> temporary(1);
        return temporary.acquire();
    }();
    CHECK(Tracked::alive == 1);
    survivor.reset();
    CHECK(Tracked::alive == 0);

    ObjectPool<int> empty(0);
    CHECK(!empty.acquire(7));
    ObjectPool<int> numbers(1);
    auto number = numbers.acquire(7);
    CHECK(number && *number == 7);

    if (failures == 0) std::cout << "All object pool tests passed.\n";
    return failures == 0 ? 0 : 1;
}

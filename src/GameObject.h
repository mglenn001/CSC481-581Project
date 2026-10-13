#ifndef GAME_OBJECT_H
#define GAME_OBJECT_H

#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>

#include "Component.h"

// GameObject is the one and only "kind" of object in the engine - a
// handle plus a bag of components. There is no inheritance tree: a
// player, the totem, a platform, an enemy, and a remote player are all
// just GameObjects with a different mix of components attached.
//
// New object types in the future (a projectile, a hazard, a pickup)
// are new combinations of existing components, or at most one new
// component - never a new class in an inheritance chain, and nothing
// here has to change to support them.
class GameObject {
public:
    explicit GameObject(std::string type, int id = -1)
        : type(std::move(type)), id(id) {}

    // Attaches a component, constructing it in place. Replaces any
    // existing component of the same type.
    template <typename T, typename... Args>
    T& addComponent(Args&&... args) {
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = component.get();
        components[std::type_index(typeid(T))] = std::move(component);
        return *raw;
    }

    // Returns the component of type T, or nullptr if this GameObject
    // doesn't have one. Systems use this to opt in/out of behavior
    // (e.g. Physics only touches objects that have a GravityComponent).
    template <typename T>
    T* getComponent() {
        auto it = components.find(std::type_index(typeid(T)));
        if (it == components.end()) {
            return nullptr;
        }
        return static_cast<T*>(it->second.get());
    }

    template <typename T>
    const T* getComponent() const {
        auto it = components.find(std::type_index(typeid(T)));
        if (it == components.end()) {
            return nullptr;
        }
        return static_cast<const T*>(it->second.get());
    }

    template <typename T>
    bool hasComponent() const {
        return components.find(std::type_index(typeid(T))) != components.end();
    }

    template <typename T>
    void removeComponent() {
        components.erase(std::type_index(typeid(T)));
    }

    const std::string& getType() const { return type; }
    int getId() const { return id; }

private:
    std::string type;   // e.g. "player", "platform", "totem" - for
                         // debugging/logging, not for branching logic
    int id;              // matches the networking clientID/PLATFORM_ID
                          // scheme already used in server.cpp/main.cpp

    std::unordered_map<std::type_index, std::unique_ptr<Component>> components;
};

#endif

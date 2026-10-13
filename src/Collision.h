#ifndef COLLISION_H
#define COLLISION_H

#include "Entity.h"
#include "GameObject.h"

class Collision {
public:
    // Check if two entities are colliding
    static bool checkCollision(const Entity& entityA, const Entity& entityB);

    // GameObject (component-based) version
    static bool checkCollision(const GameObject& a, const GameObject& b);

    // Mixed: a GameObject against a not-yet-converted Entity
    static bool checkCollision(const GameObject& a, const Entity& b);
};

#endif
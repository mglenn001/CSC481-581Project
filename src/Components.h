#ifndef COMPONENTS_H
#define COMPONENTS_H

#include <SDL3/SDL.h>
#include "Component.h"

// --- TransformComponent -----------------------------------------------
// Every GameObject has one of these. Replaces the x/y/width/height
// fields that used to live directly on Entity.
struct TransformComponent : public Component {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    TransformComponent() = default;
    TransformComponent(float x, float y, float width, float height)
        : x(x), y(y), width(width), height(height) {}
};

// --- VelocityComponent ---------------------------------------------
// Only objects that move need this. A static decoration (the totem)
// doesn't carry one at all.
struct VelocityComponent : public Component {
    float vx = 0.0f;
    float vy = 0.0f;
};

// --- GravityComponent -------------------------------------------------
// Presence of this component means "Physics::update should apply
// gravity to this object." Absence means it floats/ignores gravity -
// no bool flag to check, the system just looks for the component.
struct GravityComponent : public Component {
    bool grounded = false;
};

// --- RenderableComponent ------------------------------------------
// Everything Entity::render()/updateAnimation() needed: texture plus
// optional sprite-sheet animation state. An object with no texture
// still renders (falls back to the existing white-rect debug draw).
struct RenderableComponent : public Component {
    SDL_Texture* texture = nullptr;

    int frameCount = 1;
    int frameWidth = 0;
    int frameHeight = 0;
    int currentFrame = 0;
    int animationCounter = 0;

    float frameDuration = 0.0f;
    float animationTimer = 0.0f;
    bool useTimedAnimation = false;
};

// --- CollidableComponent ------------------------------------------
// Marks an object as something Collision::checkCollision (or platform/
// ground logic in main.cpp) should consider. `solid` distinguishes a
// physical obstacle (platform, ground, skull) from a trigger volume
// (a future pickup or checkpoint) that overlaps without blocking.
struct CollidableComponent : public Component {
    bool solid = true;
};

// --- NetworkComponent ---------------------------------------------
// Marks an object whose state crosses the wire, and how. This is the
// piece Part B's client/server code reads/writes on every endpoint
// instead of hand-special-casing object types (e.g. the reserved
// PLATFORM_ID == 0 hack in server.cpp today).
enum class NetworkAuthority {
    Local,              // this client owns it and sends its own state
    RemoteAuthoritative,// server owns it (e.g. the moving platform);
                         // clients only ever receive and render it
    RemoteMirrored       // another client owns it; this client only
                         // ever receives and renders it (other players)
};

struct NetworkComponent : public Component {
    int networkId = -1;
    NetworkAuthority authority = NetworkAuthority::Local;
};

#endif

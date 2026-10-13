#include "Collision.h"
#include <SDL3/SDL.h>
#include "Components.h"

bool Collision::checkCollision(const Entity& entityA, const Entity& entityB) {
    // Create rectangular bounds for each entity
    SDL_FRect rectA = { entityA.getX(), entityA.getY(), entityA.getWidth(), entityA.getHeight() };
    SDL_FRect rectB = { entityB.getX(), entityB.getY(), entityB.getWidth(), entityB.getHeight() };

    // SDL3's built-in AABB intersection test (bounding box collision, as recommended in the milestone)
    return SDL_HasRectIntersectionFloat(&rectA, &rectB);
}

bool Collision::checkCollision(const GameObject& a, const GameObject& b) {
    const auto* ta = a.getComponent<TransformComponent>();
    const auto* tb = b.getComponent<TransformComponent>();
    const auto* ca = a.getComponent<CollidableComponent>();
    const auto* cb = b.getComponent<CollidableComponent>();

    // Only objects that have a transform and are collidable can collide
    if (!ta || !tb || !ca || !cb) {
        return false;
    }

    SDL_FRect rectA = { ta->x, ta->y, ta->width, ta->height };
    SDL_FRect rectB = { tb->x, tb->y, tb->width, tb->height };
    return SDL_HasRectIntersectionFloat(&rectA, &rectB);
}


bool Collision::checkCollision(const GameObject& a, const Entity& b) {
    const auto* ta = a.getComponent<TransformComponent>();
    const auto* ca = a.getComponent<CollidableComponent>();

    if (!ta || !ca) {
        return false;
    }

    SDL_FRect rectA = { ta->x, ta->y, ta->width, ta->height };
    SDL_FRect rectB = { b.getX(), b.getY(), b.getWidth(), b.getHeight() };
    return SDL_HasRectIntersectionFloat(&rectA, &rectB);
}

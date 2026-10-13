#ifndef GAME_OBJECT_OPS_H
#define GAME_OBJECT_OPS_H

#include <SDL3/SDL.h>

#include "GameObject.h"
#include "Components.h"
#include "Scaling.h"

// Small helpers that do to a GameObject what Entity's methods used to do.
// Each one reads/writes the relevant component and quietly does nothing
// if the object doesn't have it, so they are safe on any GameObject.
namespace go {

inline float getX(const GameObject& o) { auto* t = o.getComponent<TransformComponent>(); return t ? t->x : 0.0f; }
inline float getY(const GameObject& o) { auto* t = o.getComponent<TransformComponent>(); return t ? t->y : 0.0f; }
inline float getWidth(const GameObject& o) { auto* t = o.getComponent<TransformComponent>(); return t ? t->width : 0.0f; }
inline float getHeight(const GameObject& o) { auto* t = o.getComponent<TransformComponent>(); return t ? t->height : 0.0f; }

inline float getVelocityY(const GameObject& o) { auto* v = o.getComponent<VelocityComponent>(); return v ? v->vy : 0.0f; }

inline void setPosition(GameObject& o, float x, float y)
{
    if (auto* t = o.getComponent<TransformComponent>()) { t->x = x; t->y = y; }
}

inline void move(GameObject& o, float dx, float dy)
{
    if (auto* t = o.getComponent<TransformComponent>()) { t->x += dx; t->y += dy; }
}

inline void setVelocity(GameObject& o, float vx, float vy)
{
    if (auto* v = o.getComponent<VelocityComponent>()) { v->vx = vx; v->vy = vy; }
}

inline void setVelocityY(GameObject& o, float vy)
{
    if (auto* v = o.getComponent<VelocityComponent>()) { v->vy = vy; }
}

inline void setGrounded(GameObject& o, bool grounded)
{
    if (auto* g = o.getComponent<GravityComponent>()) { g->grounded = grounded; }
}

inline void setTexture(GameObject& o, SDL_Texture* texture)
{
    if (auto* r = o.getComponent<RenderableComponent>()) { r->texture = texture; }
}

inline void setSpriteSheet(GameObject& o, int frameCount, int frameWidth, int frameHeight)
{
    if (auto* r = o.getComponent<RenderableComponent>()) {
        r->frameCount = frameCount;
        r->frameWidth = frameWidth;
        r->frameHeight = frameHeight;
    }
}

// Same fixed-tick animation Entity::updateAnimation() does.
inline void updateAnimation(GameObject& o)
{
    auto* r = o.getComponent<RenderableComponent>();
    if (!r || r->frameCount <= 1) {
        return;
    }
    r->animationCounter++;
    if (r->animationCounter >= 100) {
        r->currentFrame = (r->currentFrame + 1) % r->frameCount;
        r->animationCounter = 0;
    }
}

// Same drawing and scaling logic as Entity::render().
inline void render(GameObject& o, SDL_Renderer* renderer)
{
    auto* t = o.getComponent<TransformComponent>();
    auto* r = o.getComponent<RenderableComponent>();
    if (!t || !r) {
        return;
    }

    float drawX = t->x;
    float drawY = t->y;
    float drawWidth = t->width;
    float drawHeight = t->height;

    if (Scaling::getMode() == ScalingMode::PROPORTIONAL) {
        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetRenderOutputSize(renderer, &windowWidth, &windowHeight);

        float scaleX = 1.0f;
        float scaleY = 1.0f;
        Scaling::getScaleFactors(windowWidth, windowHeight, scaleX, scaleY);

        drawX = t->x * scaleX;
        drawY = t->y * scaleY;
        drawWidth = t->width * scaleX;
        drawHeight = t->height * scaleY;
    }

    SDL_FRect destinationRect = { drawX, drawY, drawWidth, drawHeight };

    if (r->texture != nullptr) {
        if (r->frameCount > 1 && r->frameWidth > 0 && r->frameHeight > 0) {
            SDL_FRect sourceRect = {
                (float)(r->currentFrame * r->frameWidth),
                0.0f,
                (float)r->frameWidth,
                (float)r->frameHeight
            };
            SDL_RenderTexture(renderer, r->texture, &sourceRect, &destinationRect);
        }
        else {
            SDL_RenderTexture(renderer, r->texture, nullptr, &destinationRect);
        }
    }
    else {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(renderer, &destinationRect);
    }
}

} // namespace go

#endif

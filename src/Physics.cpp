#include "Physics.h"

Physics::Physics(float gravity)
{
    this->gravity = gravity;
}

void Physics::setGravity(float gravity)
{
    this->gravity = gravity;
}

float Physics::getGravity() const
{
    return gravity;
}

void Physics::update(Entity& entity, float deltaTime)
{
    if (!entity.isGravityEnabled()) {
        return;
    }

    float velocityY = entity.getVelocityY();

    // Constant downward acceleration
    velocityY += gravity * deltaTime;

    entity.setVelocityY(velocityY);

    // Move entity follow its current velocity
    entity.move(
        entity.getVelocityX() * deltaTime,
        velocityY * deltaTime
    );
}

void Physics::jump(Entity& entity, float jumpStrength)
{
    if (!entity.isGrounded()) {
        return;
    }

    // Negative Y moves upward in SDL coordinates
    entity.setVelocityY(-jumpStrength);
    entity.setGrounded(false);
}

void Physics::update(GameObject& obj, float deltaTime)
{
    auto* gravityComp = obj.getComponent<GravityComponent>();
    auto* transform = obj.getComponent<TransformComponent>();
    auto* velocity = obj.getComponent<VelocityComponent>();

    // No gravity component (or nothing to move) means physics skips it
    if (!gravityComp || !transform || !velocity) {
        return;
    }

    velocity->vy += gravity * deltaTime;

    transform->x += velocity->vx * deltaTime;
    transform->y += velocity->vy * deltaTime;
}

void Physics::jump(GameObject& obj, float jumpStrength)
{
    auto* gravityComp = obj.getComponent<GravityComponent>();
    auto* velocity = obj.getComponent<VelocityComponent>();

    if (!gravityComp || !velocity || !gravityComp->grounded) {
        return;
    }

    velocity->vy = -jumpStrength;
    gravityComp->grounded = false;
}

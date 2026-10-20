#ifndef COMPONENT_H
#define COMPONENT_H

// Base class for all components. A component holds pure data (plus small
// helper methods) for one aspect of a game object's behavior - position,
// movement, how it's drawn, whether it collides, whether it's networked.
//
// Components intentionally know nothing about each other or about
// GameObject. Systems (Physics::update, Collision::checkCollision,
// rendering code, networking code) are the only things that read/write
// multiple components together, which is what keeps this from turning
// into another inheritance hierarchy.
struct Component {
    virtual ~Component() = default;
};

#endif

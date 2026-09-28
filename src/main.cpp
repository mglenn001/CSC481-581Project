#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <thread>

#include "Entity.h"
#include "Physics.h"
#include "Input.h"
#include "Collision.h"
#include "Scaling.h"
#include "Timeline.h"
#include "SharedData.h"
#include "Networking.h"

const int WINDOW_WIDTH = 1920;
const int WINDOW_HEIGHT = 1080;

// enemy_skull.png: 192x32 total, 6 frames of 32x32, 8fps
const int SKULL_FRAME_COUNT = 6;
const int SKULL_FRAME_W = 32;
const int SKULL_FRAME_H = 32;

// swirlingorb.png: 512x128 total, 4 frames of 128x128, 6fps
const int PORTAL_FRAME_COUNT = 4;
const int PORTAL_FRAME_W = 128;
const int PORTAL_FRAME_H = 128;

// totem.png: 512x192 total, 8 frames of 64x192, 8fps
const int TOTEM_FRAME_COUNT = 8;
const int TOTEM_FRAME_W = 64;
const int TOTEM_FRAME_H = 192;


int main(int argc, char *argv[])
{

    // Each game client must have its own ID.
    // Example: ./main 1 ./main 2 ./main 3
    if (argc < 2) {
        SDL_Log("Usage: ./main <clientID>");
        return 1;
    }

    int clientID = std::stoi(argv[1]);

    SDL_Log("Starting client %d", clientID);


    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Could not initialize SDL: %s", SDL_GetError());
        return 1;
    }

    // Create window and renderer
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;

    if (!SDL_CreateWindowAndRenderer(
            "Game Engine",
            WINDOW_WIDTH,
            WINDOW_HEIGHT,
            SDL_WINDOW_RESIZABLE,
            &window,
            &renderer)) {

        SDL_Log("Could not create window/renderer: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int actualWidth;
    int actualHeight;

    SDL_GetWindowSize(
        window,
        &actualWidth,
        &actualHeight
    );

    // Set the reference resolution for proportional scaling
    Scaling::setReferenceResolution(actualWidth, actualHeight);

    // Tracks whether the scale toggle key was pressed in the previous frame
    bool scaleKeyWasPressed = false;

    const float GROUND_Y = actualHeight - 150.0f;

    const float BRICK_WIDTH = 96.0f;
    const float BRICK_HEIGHT = 32.0f;

    // Area with no bricks
    const float GAP_START = 900.0f;
    const float GAP_END = 1200.0f;

    const float START_X = 100.0f;
    const float START_Y = 100.0f;

    // Create one generic entity
    Entity player(START_X, START_Y, 200.0f, 200.0f);

    // Create a Physics instance
    Physics physics;

    // Enable gravity for the player
    player.setGravityEnabled(true);
    physics.setGravity(900.0f); // Set gravity strength (pixels per second squared)

    const float PORTAL_SPAWN_X = START_X;
    const float PORTAL_SPAWN_Y = GROUND_Y - player.getHeight();

    // Totem: static object, player must jump over it
    Entity totem(400.0f, GROUND_Y - TOTEM_FRAME_H, (float)TOTEM_FRAME_W, (float)TOTEM_FRAME_H);
    totem.setGravityEnabled(false); // it's static, it never moves

    // Enemy skull: auto-moving patrol enemy, floats (no gravity)
    Entity enemySkull(GAP_START, GROUND_Y - 150.0f, (float)SKULL_FRAME_W, (float)SKULL_FRAME_H);
    enemySkull.setGravityEnabled(false);
    float skullPatrolMinX = GAP_START;
    float skullPatrolMaxX = GAP_END;
    float skullSpeed = 150.0f;
    int skullDirection = 1; // 1 = right, -1 = left

    // Portal: purely visual "respawn point" marker
    Entity portal(START_X - 40.0f, GROUND_Y - PORTAL_FRAME_H, (float)PORTAL_FRAME_W, (float)PORTAL_FRAME_H);
    portal.setGravityEnabled(false);

    // Load the player's sprite texture
    SDL_Texture* playerTexture =
        IMG_LoadTexture(renderer, "../assets/darkworld_enemy_nyx_idle.png");

    // Check if the texture loaded correctly
    if (!playerTexture) {
        SDL_Log("Could not load texture: %s", SDL_GetError());
    }
    else {
        player.setTexture(playerTexture);

        // Tell the entity how the sprite sheet is arranged
        player.setSpriteSheet(8, 128, 128);
    }

    // Load the brick
    SDL_Texture* brickTexture =
        IMG_LoadTexture(
            renderer,
            "../assets/brick.png"
        );

    if (!brickTexture) {
        SDL_Log(
            "Could not load brick texture: %s",
            SDL_GetError()
        );
    }

    // Load and assign totem textures/sprite sheets
    SDL_Texture* totemTexture = IMG_LoadTexture(renderer, "../assets/totem.png");
    if (totemTexture) {
        totem.setTexture(totemTexture);
        totem.setSpriteSheet(TOTEM_FRAME_COUNT, TOTEM_FRAME_W, TOTEM_FRAME_H);
        totem.setAnimationSpeed(8.0f);
    }

    // Load and assign skull textures/sprite sheets
    SDL_Texture* skullTexture = IMG_LoadTexture(renderer, "../assets/enemy_skull.png");
    if (skullTexture) {
        enemySkull.setTexture(skullTexture);
        enemySkull.setSpriteSheet(SKULL_FRAME_COUNT, SKULL_FRAME_W, SKULL_FRAME_H);
        enemySkull.setAnimationSpeed(8.0f);
    }

    // Load and assign portal textures/sprite sheets
    SDL_Texture* portalTexture = IMG_LoadTexture(renderer, "../assets/swirlingorb.png");
    if (portalTexture) {
        portal.setTexture(portalTexture);
        portal.setSpriteSheet(PORTAL_FRAME_COUNT, PORTAL_FRAME_W, PORTAL_FRAME_H);
        portal.setAnimationSpeed(6.0f);
    }


    // Shared data between the game loop and networking thread
    SharedData sharedData;

    sharedData.playerX = player.getX();
    sharedData.playerY = player.getY();

    // Start networking in its own thread
    std::thread networkThread(
        networkingThread,
        std::ref(sharedData),
        clientID
    );

    SDL_Log(
        "Client %d networking thread created",
        clientID
    );


    bool running = true;
    SDL_Event event;

    // Uint64 lastTime = SDL_GetTicks();
    // game timeline anchored to real time
    Timeline gameTime;

    // used so one key press only trigger once
    bool pauseKeyWasPressed = false;
    bool minusKeyWasPressed = false;
    bool plusKeyWasPressed = false;


    // Main game loop
    while (running) {

        // Check if user closes window
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        // Get elapsed game time from the timeline
        float deltaTime = static_cast<float>(gameTime.getDeltaTime());

        // A: walk left
        // D: walk right
        // W: jump
        // S: crouch
        // Space: Attack
        // W + A: jump left
        // W + D: jump right
        // Shift + A: run left
        // Shift + D: run right
        // P: Pause / unpause game time
        // - = Decrease game speed
        // + = Increase game speed

        // P: Pause / unpause game time
        bool pauseKeyIsPressed = Input::isKeyPressed(SDL_SCANCODE_P);
        if (pauseKeyIsPressed && !pauseKeyWasPressed) {
            if (gameTime.isPaused()) {
                gameTime.unpause();
                SDL_Log("Game resumes");
            } else {
                gameTime.pause();
                SDL_Log("Game paused");
            }
        }
        pauseKeyWasPressed = pauseKeyIsPressed;

        // cycle game speed
        // - = Decrease game speed
        // + = Increase game speed
        bool minusKeyIsPressed =
            Input::isKeyPressed(SDL_SCANCODE_MINUS);

        bool plusKeyIsPressed =
            Input::isKeyPressed(SDL_SCANCODE_EQUALS);

        // Decrease speed: 2.0x -> 1.0x -> 0.5x
        if (minusKeyIsPressed && !minusKeyWasPressed) {

            double currentScale = gameTime.getScale();

            if (currentScale == 2.0) {
                gameTime.setScale(1.0);
            }
            else if (currentScale == 1.0) {
                gameTime.setScale(0.5);
            }

            SDL_Log(
                "Game time scale: %.1fx",
                gameTime.getScale()
            );
        }

        // Increase speed: 0.5x -> 1.0x -> 2.0x
        if (plusKeyIsPressed && !plusKeyWasPressed) {

            double currentScale = gameTime.getScale();

            if (currentScale == 0.5) {
                gameTime.setScale(1.0);
            }
            else if (currentScale == 1.0) {
                gameTime.setScale(2.0);
            }

            SDL_Log(
                "Game time scale: %.1fx",
                gameTime.getScale()
            );
        }

        minusKeyWasPressed = minusKeyIsPressed;
        plusKeyWasPressed = plusKeyIsPressed;


        // Toggle scaling mode with the T key
        bool scaleKeyIsPressed = Input::isKeyPressed(SDL_SCANCODE_T);
        if (scaleKeyIsPressed && !scaleKeyWasPressed) {
            Scaling::toggleMode();
            SDL_Log(
                "Scaling mode: %s",
                (Scaling::getMode() == ScalingMode::PROPORTIONAL) ? "PROPORTIONAL" : "PIXEL"
            );
        }
        scaleKeyWasPressed = scaleKeyIsPressed;

        const float WALK_SPEED = 300.0f;
        const float RUN_SPEED = 550.0f;
        float moveSpeed = WALK_SPEED;

        if (Input::isKeyPressed(SDL_SCANCODE_LSHIFT) ||
            Input::isKeyPressed(SDL_SCANCODE_RSHIFT)) {
            moveSpeed = RUN_SPEED;
        }

        if (!gameTime.isPaused()) { 
            // Input for Jumping (W key)
            if (Input::isKeyPressed(SDL_SCANCODE_W)) {
                physics.jump(player, 650.0f);
            }

            // Input for moving left and right (A and D keys)
            if (Input::isKeyPressed(SDL_SCANCODE_A)) {
                player.move(-moveSpeed * deltaTime, 0.0f);
            }

            if (Input::isKeyPressed(SDL_SCANCODE_D)) {
                player.move(moveSpeed * deltaTime, 0.0f);
            }

            // Input for crouching (S key)
            if (Input::isKeyPressed(SDL_SCANCODE_S)) {
                SDL_Log("S pressed - crouch action");
            }

            // Input for attacking (Space key)
            if (Input::isKeyPressed(SDL_SCANCODE_SPACE)) {
                SDL_Log("Attack!");
            }
        }

        // Update physics
        physics.update(player, deltaTime);

        // Ground check
        float playerLeft = player.getX();

        float playerRight = player.getX() + player.getWidth();

        bool overGap = playerRight > GAP_START && playerLeft < GAP_END;

        // If player is NOT over the gap,
        // let them land on the brick ground
        if (
            !overGap &&
            player.getY() + player.getHeight() >= GROUND_Y &&
            player.getVelocityY() >= 0.0f
        ) {

            player.setPosition(
                player.getX(),
                GROUND_Y - player.getHeight()
            );

            player.setVelocityY(0.0f);

            player.setGrounded(true);
        }
        else {
            player.setGrounded(false);
        }

        // Fail/Reset
        if (player.getY() > actualHeight) {

            SDL_Log(
                "Player fell! Respawning at the portal."
            );

            player.setPosition(
                PORTAL_SPAWN_X,
                PORTAL_SPAWN_Y
            );

            player.setVelocity(
                0.0f,
                0.0f
            );

            player.setGrounded(true);
        }

        // Collision: totem blocks the player like a wall
        if (Collision::checkCollision(player, totem)) {
            if (Input::isKeyPressed(SDL_SCANCODE_A)) {
                player.move(moveSpeed * deltaTime, 0.0f);
            }
            if (Input::isKeyPressed(SDL_SCANCODE_D)) {
                player.move(-moveSpeed * deltaTime, 0.0f);
            }
        }

        // Collision: enemy (ex: skull) sends player (ex: nxy) back to the portal
        if (Collision::checkCollision(player, enemySkull)) {
            SDL_Log("Player touched an enemy! Respawning at the portal.");
            player.setPosition(
                PORTAL_SPAWN_X,
                PORTAL_SPAWN_Y
            );
            player.setVelocity(
                0.0f,
                0.0f
            );
            player.setGrounded(true);
        }


        // Share this player's latest position with the networking thread
        {
            std::lock_guard<std::mutex> lock(
                sharedData.playerMutex
            );

            sharedData.playerX = player.getX();
            sharedData.playerY = player.getY();
        }

        // Enemy patrol movement
        enemySkull.move(skullSpeed * skullDirection * deltaTime, 0.0f);
        if (enemySkull.getX() >= skullPatrolMaxX) {
            skullDirection = -1;
        }
        if (enemySkull.getX() <= skullPatrolMinX) {
            skullDirection = 1;
        }

        // Set background color to sage green
        SDL_SetRenderDrawColor(renderer, 169, 186, 157, 255);

        // Clear previous frame
        SDL_RenderClear(renderer);

        // Render the brick ground
        if (brickTexture) {
            float groundScaleX = 1.0f;
            float groundScaleY = 1.0f;
            
            if (Scaling::getMode() == ScalingMode::PROPORTIONAL) {
                int windowWidth = 0;
                int windowHeight = 0;
                SDL_GetRenderOutputSize(renderer, &windowWidth, &windowHeight);
                Scaling::getScaleFactors(windowWidth, windowHeight, groundScaleX, groundScaleY);
            }

            for (
                float x = 0.0f;
                x < WINDOW_WIDTH;
                x += BRICK_WIDTH
            ) {

                // Leave a gap in the ground
                if (
                    x + BRICK_WIDTH > GAP_START &&
                    x < GAP_END
                ) {
                    continue;
                }

                SDL_FRect brickRect = {
                    x * groundScaleX,
                    GROUND_Y * groundScaleY,
                    BRICK_WIDTH * groundScaleX,
                    BRICK_HEIGHT * groundScaleY
                };

                SDL_RenderTexture(
                    renderer,
                    brickTexture,
                    nullptr,
                    &brickRect
                );
            }
        }

        // Update the sprite animation
        player.updateAnimation();
        enemySkull.updateAnimation(deltaTime);
        totem.updateAnimation(deltaTime);
        portal.updateAnimation(deltaTime);

        // Render the entity
        portal.render(renderer);
        totem.render(renderer);
        enemySkull.render(renderer);
        // Render characters controlled by the other clients.
        {
            std::lock_guard<std::mutex> lock(
                sharedData.playerMutex
            );

            for (const auto& entry : sharedData.remotePlayers) {

                const RemotePlayerState& remote =
                    entry.second;

                Entity remotePlayer(
                    remote.x,
                    remote.y,
                    player.getWidth(),
                    player.getHeight()
                );

                remotePlayer.setTexture(playerTexture);
                remotePlayer.setSpriteSheet(8, 128, 128);
                remotePlayer.render(renderer);
            }
        }
        player.render(renderer);

        // Show the frame
        SDL_RenderPresent(renderer);
    }

    // Clean up
    if (playerTexture) {
        SDL_DestroyTexture(playerTexture);
    }
    if (brickTexture) {
        SDL_DestroyTexture(brickTexture);
    }
    if (totemTexture) {
        SDL_DestroyTexture(totemTexture);
    }
    if (skullTexture) {
        SDL_DestroyTexture(skullTexture);
    }
    if (portalTexture) {
        SDL_DestroyTexture(portalTexture);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    // Tell networking thread to stop
    sharedData.running.store(false);

    // Wait for networking thread to finish
    if (networkThread.joinable()) {
        networkThread.join();
    }
    
    SDL_Quit();

    return 0;
}
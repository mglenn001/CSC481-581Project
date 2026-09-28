#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <thread>
#include <cmath>
#include <chrono>

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

// Platform patrol boundaries
const float PLATFORM_MIN_X = 600.0f;
const float PLATFORM_MAX_X = 800.0f;


int main(int argc, char *argv[])
{

    // Each game client must have its own ID.
    // Example: ./main 1 ./main 2 ./main 3
    if (argc < 2) {
        SDL_Log("Usage: ./main <clientID>");
        return 1;
    }

    int clientID = std::stoi(argv[1]);

    SDL_Log("Starting peer client %d", clientID);

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
    physics.setGravity(900.0f);

    const float PORTAL_SPAWN_X = START_X;
    const float PORTAL_SPAWN_Y = GROUND_Y - player.getHeight();

    // Totem: static object, player must jump over it
    Entity totem(
        400.0f,
        GROUND_Y - TOTEM_FRAME_H,
        (float)TOTEM_FRAME_W,
        (float)TOTEM_FRAME_H
    );

    totem.setGravityEnabled(false);

    // Enemy skull: auto-moving patrol enemy
    Entity enemySkull(
        GAP_START,
        GROUND_Y - 150.0f,
        (float)SKULL_FRAME_W,
        (float)SKULL_FRAME_H
    );

    enemySkull.setGravityEnabled(false);

    float skullPatrolMinX = GAP_START;
    float skullPatrolMaxX = GAP_END;
    float skullSpeed = 150.0f;
    int skullDirection = 1;

    // Portal: visual respawn point
    Entity portal(
        START_X - 40.0f,
        GROUND_Y - PORTAL_FRAME_H,
        (float)PORTAL_FRAME_W,
        (float)PORTAL_FRAME_H
    );

    portal.setGravityEnabled(false);

    // Moving platform: position comes entirely from the server
    // (see SharedData.platformX/Y), never computed locally and
    // never tied to this client's own Timeline. This is what
    // keeps it in the same place for every client no matter
    // that client's individual speed.
    const float PLATFORM_WIDTH = 150.0f;
    const float PLATFORM_HEIGHT = 40.0f;

    Entity platform(
        600.0f,
        GROUND_Y - 200.0f,
        PLATFORM_WIDTH,
        PLATFORM_HEIGHT
    );

    platform.setGravityEnabled(false);

    // Tracks the platform's previous X so the player can be
    // carried along by however far it moved this frame.
    float previousPlatformX = platform.getX();


    // Load the player's sprite texture
    SDL_Texture* playerTexture = IMG_LoadTexture(renderer, "../assets/darkworld_enemy_nyx_idle.png");

    if (!playerTexture) {
        SDL_Log("Could not load texture: %s",SDL_GetError());
    }
    else {
        player.setTexture(playerTexture);
        player.setSpriteSheet(8, 128, 128);
    }

    // Load the brick
    SDL_Texture* brickTexture = IMG_LoadTexture(renderer,"../assets/brick.png");

    if (!brickTexture) {
        SDL_Log("Could not load brick texture: %s",SDL_GetError());
    }

    // Load totem texture
    SDL_Texture* totemTexture = IMG_LoadTexture(renderer,"../assets/totem.png");

    if (totemTexture) {
        totem.setTexture(totemTexture);
        totem.setSpriteSheet(
            TOTEM_FRAME_COUNT,
            TOTEM_FRAME_W,
            TOTEM_FRAME_H
        );
        totem.setAnimationSpeed(8.0f);
    }

    // Load skull texture
    SDL_Texture* skullTexture = IMG_LoadTexture(renderer,"../assets/enemy_skull.png");

    if (skullTexture) {
        enemySkull.setTexture(skullTexture);
        enemySkull.setSpriteSheet(
            SKULL_FRAME_COUNT,
            SKULL_FRAME_W,
            SKULL_FRAME_H
        );
        enemySkull.setAnimationSpeed(8.0f);
    }


    // Load portal texture
    SDL_Texture* portalTexture = IMG_LoadTexture(renderer,"../assets/swirlingorb.png");

    if (portalTexture) {
        portal.setTexture(portalTexture);
        portal.setSpriteSheet(
            PORTAL_FRAME_COUNT,
            PORTAL_FRAME_W,
            PORTAL_FRAME_H
        );
        portal.setAnimationSpeed(6.0f);
    }

    // Shared data between the game loop and networking thread
    SharedData sharedData;
    sharedData.playerX = player.getX();
    sharedData.playerY = player.getY();

    // Start networking in its own thread
    std::thread networkThread(networkingThread,std::ref(sharedData),clientID);

    SDL_Log("Client %d networking thread created",clientID);

    bool running = true;
    SDL_Event event;

    // Game timeline anchored to real time
    Timeline gameTime;

    // Used so one key press only triggers once
    bool pauseKeyWasPressed = false;
    bool minusKeyWasPressed = false;
    bool plusKeyWasPressed = false;

    // Record base clock start time for deterministic platform sync across peers
    auto startTime = std::chrono::steady_clock::now();

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

        /*
         * Controls
         *
         * A = left
         * D = right
         * W = jump
         * Shift = run
         * P = pause / unpause
         * - = decrease game speed
         * + = increase game speed
         * T = scaling mode
         */

        // Pause / unpause
        bool pauseKeyIsPressed = Input::isKeyPressed(SDL_SCANCODE_P);
        if (pauseKeyIsPressed && !pauseKeyWasPressed) {
            if (gameTime.isPaused()) {
                gameTime.unpause();
                SDL_Log("Game resumes");
            }
            else {
                gameTime.pause();
                SDL_Log("Game paused");
            }
        }
        pauseKeyWasPressed = pauseKeyIsPressed;

        // Timeline scaling
        bool minusKeyIsPressed = Input::isKeyPressed(SDL_SCANCODE_MINUS);
        bool plusKeyIsPressed = Input::isKeyPressed(SDL_SCANCODE_EQUALS);
        // Decrease:
        // 2.0x -> 1.0x -> 0.5x
        if (minusKeyIsPressed && !minusKeyWasPressed) {
            double currentScale = gameTime.getScale();

            if (currentScale == 2.0) {
                gameTime.setScale(1.0);
            }
            else if (currentScale == 1.0) {
                gameTime.setScale(0.5);
            }

            SDL_Log("Game time scale: %.1fx",gameTime.getScale());
        }
        // Increase:
        // 0.5x -> 1.0x -> 2.0x
        if (plusKeyIsPressed && !plusKeyWasPressed) {
            double currentScale =
                gameTime.getScale();

            if (currentScale == 0.5) {
                gameTime.setScale(1.0);
            }
            else if (currentScale == 1.0) {
                gameTime.setScale(2.0);
            }

            SDL_Log("Game time scale: %.1fx",gameTime.getScale());
        }
        minusKeyWasPressed = minusKeyIsPressed;
        plusKeyWasPressed = plusKeyIsPressed;

        /*
         * Section 4:
         * Share this client's current timeline speed
         * with the networking thread.
         *
         * This allows each client to communicate
         * with the server at a rate based on its
         * own timeline scale.
         */
        sharedData.timeScale.store(gameTime.getScale());

        // Toggle scaling mode with T
        bool scaleKeyIsPressed = Input::isKeyPressed(SDL_SCANCODE_T);

        if (scaleKeyIsPressed && !scaleKeyWasPressed) {
            Scaling::toggleMode();

            SDL_Log("Scaling mode: %s",(Scaling::getMode() == ScalingMode::PROPORTIONAL) ? "PROPORTIONAL" : "PIXEL");
        }
        scaleKeyWasPressed = scaleKeyIsPressed;

        const float WALK_SPEED = 300.0f;
        const float RUN_SPEED = 550.0f;

        float moveSpeed = WALK_SPEED;

        if (Input::isKeyPressed(SDL_SCANCODE_LSHIFT) || Input::isKeyPressed(SDL_SCANCODE_RSHIFT)) {
            moveSpeed = RUN_SPEED;
        }

        if (!gameTime.isPaused()) {
            // Jump
            if (Input::isKeyPressed(SDL_SCANCODE_W)) {
                physics.jump(player,650.0f);
            }

            // Move left
            if (Input::isKeyPressed(SDL_SCANCODE_A)) {
                player.move(-moveSpeed * deltaTime,0.0f);
            }

            // Move right
            if (Input::isKeyPressed(SDL_SCANCODE_D)) {
                player.move(moveSpeed * deltaTime,0.0f);
            }

            // Crouch placeholder
            if (Input::isKeyPressed(SDL_SCANCODE_S)) {
                SDL_Log("S pressed - crouch action");
            }

            // Attack placeholder
            if (Input::isKeyPressed(SDL_SCANCODE_SPACE)) {
                SDL_Log("Attack!");
            }
        }

        // Update physics
        physics.update(player,deltaTime);

        // Ground check
        float playerLeft = player.getX();
        float playerRight = player.getX() + player.getWidth();
        bool overGap = playerRight > GAP_START && playerLeft < GAP_END;

        if (!overGap && player.getY() + player.getHeight() >= GROUND_Y && player.getVelocityY() >= 0.0f) {
            player.setPosition(player.getX(),GROUND_Y - player.getHeight());
            player.setVelocityY(0.0f);
            player.setGrounded(true);
        }
        else {
            player.setGrounded(false);
        }

        // Section 5: Full P2P no server platform synchronization
        // Calculate total elapsed wall-clock time since program startup
        auto now = std::chrono::steady_clock::now();
        float totalElapsedSeconds = std::chrono::duration<float>(now - startTime).count();

        // Deterministic oscillation function based on real wall-clock time
        float midPoint = (PLATFORM_MIN_X + PLATFORM_MAX_X) / 2.0f;
        float amplitude = (PLATFORM_MAX_X - PLATFORM_MIN_X) / 2.0f;

        // Sine wave ensures every peer computes the exact same platform position independently
        float newPlatformX = midPoint + amplitude * std::sin(totalElapsedSeconds * 1.5f);
        float newPlatformY = GROUND_Y - 200.0f;

        float platformDeltaX = newPlatformX - previousPlatformX;

        platform.setPosition(newPlatformX, newPlatformY);

        previousPlatformX = newPlatformX;

        // Platform collision: stand on top of it and get
        // carried along as it moves.
        bool horizontallyOverPlatform = playerRight > platform.getX() && playerLeft < platform.getX() + platform.getWidth();

        bool landingOnPlatform = player.getY() + player.getHeight() <= platform.getY() + 20.0f &&
            player.getY() + player.getHeight() >= platform.getY() - 20.0f && player.getVelocityY() >= 0.0f;

        if (horizontallyOverPlatform && landingOnPlatform) {
            player.setPosition(player.getX() + platformDeltaX, platform.getY() - player.getHeight());

            player.setVelocityY(0.0f);
            player.setGrounded(true);
        }

        // Fall reset
        if (player.getY() > actualHeight) {
            SDL_Log("Player fell! Respawning at the portal.");

            player.setPosition(PORTAL_SPAWN_X,PORTAL_SPAWN_Y);
            player.setVelocity(0.0f,0.0f);
            player.setGrounded(true);
        }

        // Totem collision
        if (Collision::checkCollision(player,totem)) {
            if (Input::isKeyPressed(SDL_SCANCODE_A)) {
                player.move(moveSpeed * deltaTime,0.0f);
            }

            if (Input::isKeyPressed(SDL_SCANCODE_D)) {
                player.move(-moveSpeed * deltaTime,0.0f);
            }
        }

        // Enemy collision
        if (Collision::checkCollision(player,enemySkull)) {
            SDL_Log("Player touched an enemy! Respawning at the portal.");

            player.setPosition(PORTAL_SPAWN_X,PORTAL_SPAWN_Y);

            player.setVelocity(0.0f,0.0f);

            player.setGrounded(true);
        }

        // Share local player position
        {
            std::lock_guard<std::mutex> lock(sharedData.playerMutex);
            sharedData.playerX = player.getX();
            sharedData.playerY = player.getY();
        }

        // Enemy patrol movement
        enemySkull.move(skullSpeed * skullDirection * deltaTime,0.0f);

        if (enemySkull.getX() >= skullPatrolMaxX) {
            skullDirection = -1;
        }

        if (enemySkull.getX() <= skullPatrolMinX ) {
            skullDirection = 1;
        }

        // Background color
        SDL_SetRenderDrawColor(
            renderer,
            169,
            186,
            157,
            255
        );

        SDL_RenderClear(renderer);

        // Render brick ground
        if (brickTexture) {
            float groundScaleX = 1.0f;
            float groundScaleY = 1.0f;

            if (Scaling::getMode() == ScalingMode::PROPORTIONAL) {
                int windowWidth = 0;
                int windowHeight = 0;

                SDL_GetRenderOutputSize(
                    renderer,
                    &windowWidth,
                    &windowHeight
                );

                Scaling::getScaleFactors(
                    windowWidth,
                    windowHeight,
                    groundScaleX,
                    groundScaleY
                );
            }

            for (float x = 0.0f; x < WINDOW_WIDTH; x += BRICK_WIDTH) {
                // Leave gap in ground
                if (x + BRICK_WIDTH > GAP_START && x < GAP_END) {
                    continue;
                }

                SDL_FRect brickRect = {x * groundScaleX,GROUND_Y * groundScaleY,BRICK_WIDTH * groundScaleX,BRICK_HEIGHT * groundScaleY};

                SDL_RenderTexture(
                    renderer,
                    brickTexture,
                    nullptr,
                    &brickRect
                );
            }
        }

        // Update animations
        player.updateAnimation();
        enemySkull.updateAnimation(deltaTime);
        totem.updateAnimation(deltaTime);
        portal.updateAnimation(deltaTime);
        platform.updateAnimation(deltaTime);

        // Render objects
        portal.render(renderer);
        totem.render(renderer);
        enemySkull.render(renderer);
        platform.render(renderer);

        // Render positions received directly from remote peers
        {
            std::lock_guard<std::mutex> lock(sharedData.playerMutex);

            for (const auto& entry : sharedData.remotePlayers) {
                const RemotePlayerState& remote = entry.second;

                Entity remotePlayer(
                    remote.x,
                    remote.y,
                    player.getWidth(),
                    player.getHeight()
                );

                remotePlayer.setTexture(playerTexture);
                remotePlayer.setSpriteSheet(8,128,128);
                remotePlayer.render(renderer);
            }
        }

        // Render local player
        player.render(renderer);
        SDL_RenderPresent(renderer);
    }

    // Cleanup textures
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

    // Wait for networking thread
    if (networkThread.joinable()) {
        networkThread.join();
    }

    SDL_Quit();
    return 0;
}
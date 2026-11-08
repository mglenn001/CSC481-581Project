#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <thread>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <string>
#include <vector>

#include "Entity.h"
#include "Physics.h"
#include "Input.h"
#include "Collision.h"
#include "Scaling.h"
#include "Timeline.h"
#include "EventManager.h"
#include "ReplaySystem.h"
#include "SharedData.h"
#include "Networking.h"
#include "GameObjectFactory.h"
#include "GameObjectOps.h"

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

// Milestone 4 Section 1.3: IDs used for objects in replay frames.
// Players use their client ID (1-4).
const int REPLAY_SKULL_ID = 100;
const int REPLAY_PLATFORM_ID = 101;

// Platform patrol boundaries
const float PLATFORM_MIN_X = 600.0f;
const float PLATFORM_MAX_X = 800.0f;

int main(int argc, char *argv[])
{
    // Each game client must have its own ID.
    // Example: ./main 1 ./main 2 ./main 3
    // Milestone 3 Section C.1: Optional second argument picks the network data format.
    // Example: ./main 1 binary
    if (argc < 2) {
        SDL_Log("Usage: ./main <clientID> [text|binary]");
        return 1;
    }

    int clientID = std::stoi(argv[1]);

    // Default is TEXT (the original format). "binary" selects the raw-struct format.
    NetworkFormat networkFormat = NetworkFormat::TEXT;
    if (argc >= 3) {
        networkFormat = NetFormat::parseFormat(argv[2]);
    }

    if (clientID < 1 || clientID > 4) {
        SDL_Log("Client ID must be between 1 and 4");
        return 1;
    }
    SDL_Log("Starting peer client %d (network format: %s)", clientID, NetFormat::formatName(networkFormat));

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
    GameObject player = makePlayer(clientID, START_X, START_Y, 200.0f, 200.0f);

    // Create a Physics instance
    Physics physics;

    // Gravity comes from the player having a GravityComponent
    physics.setGravity(900.0f);

    const float PORTAL_SPAWN_X = START_X;
    const float PORTAL_SPAWN_Y = GROUND_Y - go::getHeight(player);

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

    // Shared patrol settings for all P2P clients
    const float skullPatrolMinX = GAP_START;
    const float skullPatrolMaxX = GAP_END;
    const float skullSpeed = 150.0f;

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

    GameObject platform = makePlatform(
        600.0f,
        GROUND_Y - 200.0f,
        PLATFORM_WIDTH,
        PLATFORM_HEIGHT
    );

    // Tracks the platform's previous X so the player can be
    // carried along by however far it moved this frame.
    float previousPlatformX = go::getX(platform);

    // Load the player's sprite texture
    SDL_Texture* playerTexture = IMG_LoadTexture(renderer, "../assets/darkworld_enemy_nyx_idle.png");
    if (!playerTexture) {
        SDL_Log("Could not load texture: %s",SDL_GetError());
    }
    else {
        go::setTexture(player, playerTexture);
        go::setSpriteSheet(player, 8, 128, 128);
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
    sharedData.networkFormat = networkFormat; // Section C.1: format used by the networking thread
    sharedData.playerX = go::getX(player);
    sharedData.playerY = go::getY(player);

    // Milestone 4 Section 1.2: The event manager must exist BEFORE the networking
    // thread starts, because that thread raises events received from other clients.
    EventManager events;
    sharedData.eventManager = &events; // incoming events are raised here
    // Outgoing: raiseNetworked() hands each event to this function, which only
    // queues it. The networking thread does the actual sending.
    events.setNetworkSink([&](const GameEvent& e) { sharedData.queueOutgoingEvent(e); });

    // Start networking in its own thread
    std::thread networkThread(networkingThread,std::ref(sharedData),clientID);
    SDL_Log("Client %d networking thread created",clientID);

    bool running = true;
    SDL_Event event;

    // Game timeline anchored to real time
    Timeline gameTime;

    // Milestone 4 Section 1.1: each P2P client owns an event manager (created above).
    auto raiseEvent = [&](EventType type, EventPriority priority,
                          const std::string& detail, float x = 0.0f, float y = 0.0f) {
        events.raise(GameEvent{type, priority, gameTime.getTime(), clientID,
                               clientID, detail, x, y});
    };

    // Milestone 4 Section 1.2: Same as raiseEvent, but the event is ALSO sent to
    // every other client. Used for events other players should see (death, spawn).
    auto raiseNetworkedEvent = [&](EventType type, EventPriority priority,
                                   const std::string& detail, float x = 0.0f, float y = 0.0f) {
        events.raiseNetworked(GameEvent{type, priority, gameTime.getTime(), clientID,
                                        clientID, detail, x, y});
    };

    // Milestone 4 Section 1.2: Short on-screen messages about events that
    // happened on OTHER clients. Only the game thread touches this list.
    struct Notice { std::string text; Uint64 expiresAt; };
    std::vector<Notice> notices;
    auto addNotice = [&](const std::string& text) {
        notices.push_back({text, SDL_GetTicks() + 3000}); // show for 3 seconds
        SDL_Log("%s", text.c_str());
    };

    // Event handlers update the game state, rather than collision/input code
    // directly performing those actions.
    events.subscribe(EventType::Input, [&](const GameEvent& e) {
        if (e.detail == "jump") physics.jump(player, 650.0f);
        else if (e.detail == "left") go::move(player, -e.x, 0.0f);
        else if (e.detail == "right") go::move(player, e.x, 0.0f);
        else if (e.detail == "attack") SDL_Log("Input event: attack");
        else if (e.detail == "crouch") SDL_Log("Input event: crouch");
    });
    events.subscribe(EventType::Collision, [&](const GameEvent& e) {
        if (e.detail == "totem") {
            if (Input::isKeyPressed(SDL_SCANCODE_A)) go::move(player, e.x, 0.0f);
            if (Input::isKeyPressed(SDL_SCANCODE_D)) go::move(player, -e.x, 0.0f);
        } else if (e.detail == "enemy") {
            SDL_Log("Collision event: player touched enemy");
            raiseNetworkedEvent(EventType::Death, EventPriority::Critical, "enemy",
                                go::getX(player), go::getY(player));
        }
    });
    events.subscribe(EventType::Death, [&](const GameEvent& e) {
        // Event came from another client: just tell the player. Their own
        // client already respawns them, and we show their new position via the
        // normal position messages. We must NOT respawn our own player here.
        if (e.sourceId != clientID) {
            addNotice("Player " + std::to_string(e.sourceId) + " died (" + e.detail + ")");
            return;
        }
        SDL_Log("Death event (%s): respawn requested", e.detail.c_str());
        raiseNetworkedEvent(EventType::Spawn, EventPriority::High, "portal",
                            PORTAL_SPAWN_X, PORTAL_SPAWN_Y);
    });
    events.subscribe(EventType::Spawn, [&](const GameEvent& e) {
        // Event came from another client: only show a message, don't move our player.
        if (e.sourceId != clientID) {
            addNotice("Player " + std::to_string(e.sourceId) + " spawned (" + e.detail + ")");
            return;
        }
        go::setPosition(player, e.x, e.y);
        go::setVelocity(player, 0.0f, 0.0f);
        go::setGrounded(player, true);
        SDL_Log("Spawn event: player returned to portal");
    });
    // Spawn event at startup demonstrates registration/raising/handling.
    raiseNetworkedEvent(EventType::Spawn, EventPriority::High, "initial",
                        PORTAL_SPAWN_X, PORTAL_SPAWN_Y);
    events.dispatch();

    // Milestone 4 Section 1.3: Replay system, built on the event manager.
    // It is controlled only by replay events (see ReplaySystem.h):
    //   R = start / stop recording
    //   V = start / stop playback
    //   - / = during playback change the replay speed (0.5x, 1x, 2x)
    ReplaySystem replay(events, [&]() { return gameTime.getTime(); });

    // Recording and playback run on their own real-time Timeline, so the replay
    // speed does not depend on the game timeline's pause or speed settings.
    Timeline replayClock;

    // Other players' positions while a replay is playing
    std::vector<ReplayObjectState> replayRemotePlayers;

    // What one frame of the recording stores
    replay.setSnapshotSource([&]() {
        std::vector<ReplayObjectState> objects;
        const auto* velocity = player.getComponent<VelocityComponent>();
        objects.push_back({clientID, go::getX(player), go::getY(player),
                           velocity ? velocity->vx : 0.0f, velocity ? velocity->vy : 0.0f});
        objects.push_back({REPLAY_SKULL_ID, enemySkull.getX(), enemySkull.getY(), 0.0f, 0.0f});
        objects.push_back({REPLAY_PLATFORM_ID, go::getX(platform), go::getY(platform), 0.0f, 0.0f});
        std::lock_guard<std::mutex> lock(sharedData.playerMutex);
        for (const auto& entry : sharedData.remotePlayers) {
            objects.push_back({entry.first, entry.second.x, entry.second.y, 0.0f, 0.0f});
        }
        return objects;
    });

    // How a recorded frame is shown (also used to restore the live game afterwards)
    replay.setApplyFrame([&](const ReplayFrame& frame) {
        replayRemotePlayers.clear();
        for (const ReplayObjectState& o : frame.objects) {
            if (o.id == clientID) {
                go::setPosition(player, o.x, o.y);
                go::setVelocity(player, o.vx, o.vy);
            } else if (o.id == REPLAY_SKULL_ID) {
                enemySkull.setPosition(o.x, o.y);
            } else if (o.id == REPLAY_PLATFORM_ID) {
                go::setPosition(platform, o.x, o.y);
                previousPlatformX = o.x;
            } else {
                replayRemotePlayers.push_back(o);
            }
        }
    });

    // Recorded gameplay events are shown again at the moment they happened.
    // Input events are skipped because there is one for every frame a key is held.
    replay.setEventPlayback([&](const GameEvent& e) {
        const char* name = nullptr;
        switch (e.type) {
            case EventType::Collision: name = "Collision"; break;
            case EventType::Death: name = "Death"; break;
            case EventType::Spawn: name = "Spawn"; break;
            default: break;
        }
        if (!name) return;
        std::string who = (e.sourceId == clientID) ? "you" : "player " + std::to_string(e.sourceId);
        std::string text = std::string("REPLAY ") + name + ": " + e.detail + " (" + who + ")";
        // A collision is raised every frame while touching; show it once while it is on screen
        for (const Notice& n : notices) {
            if (n.text == text) return;
        }
        addNotice(text);
    });

    replay.setStatusCallback([&](const std::string& text) { addNotice(text); });

    bool recordKeyWasPressed = false;
    bool playKeyWasPressed = false;

    // Used so one key press only triggers once
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
        // Milestone 4 Section 1.3: during playback, - and = change the replay speed
        if (replay.isPlaying()) {
            if (minusKeyIsPressed && !minusKeyWasPressed)
                replay.requestSpeed(replay.speed() > 1.0f ? 1.0f : 0.5f);
            if (plusKeyIsPressed && !plusKeyWasPressed)
                replay.requestSpeed(replay.speed() < 1.0f ? 1.0f : 2.0f);
        }
        // Decrease: 2.0x -> 1.0x -> 0.5x
        else if (minusKeyIsPressed && !minusKeyWasPressed) {
            double currentScale = gameTime.getScale();

            if (currentScale == 2.0) {
                gameTime.setScale(1.0);
            }
            else if (currentScale == 1.0) {
                gameTime.setScale(0.5);
            }

            SDL_Log("Game time scale: %.1fx",gameTime.getScale());
        }
        // Increase: 0.5x -> 1.0x -> 2.0x
        if (!replay.isPlaying() && plusKeyIsPressed && !plusKeyWasPressed) {
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

        // Milestone 4 Section 1.3: replay keys only RAISE replay events.
        // The replay system handles them during dispatch.
        bool recordKeyIsPressed = Input::isKeyPressed(SDL_SCANCODE_R);
        if (recordKeyIsPressed && !recordKeyWasPressed) {
            if (replay.isRecording()) replay.requestRecordStop();
            else replay.requestRecordStart();
        }
        recordKeyWasPressed = recordKeyIsPressed;

        bool playKeyIsPressed = Input::isKeyPressed(SDL_SCANCODE_V);
        if (playKeyIsPressed && !playKeyWasPressed) {
            if (replay.isPlaying()) replay.requestPlaybackStop();
            else replay.requestPlaybackStart(1.0f);
        }
        playKeyWasPressed = playKeyIsPressed;

        // Called every frame so the replay clock never jumps
        double replayDelta = replayClock.getDeltaTime();

        // While a replay plays, the live game is frozen: no input, physics,
        // collisions or movers. The replay system moves the objects instead.
        if (!replay.isPlaying()) {

        if (!gameTime.isPaused()) {
            // SDL is polled for key state only; gameplay is handled by our event queue.
            if (Input::isKeyPressed(SDL_SCANCODE_W))
                raiseEvent(EventType::Input, EventPriority::Normal, "jump");
            if (Input::isKeyPressed(SDL_SCANCODE_A))
                raiseEvent(EventType::Input, EventPriority::Normal, "left", moveSpeed * deltaTime);
            if (Input::isKeyPressed(SDL_SCANCODE_D))
                raiseEvent(EventType::Input, EventPriority::Normal, "right", moveSpeed * deltaTime);
            // Edge-trigger nonmovement inputs to avoid flooding the queue.
            static bool wasCrouch = false, wasAttack = false;
            bool crouch = Input::isKeyPressed(SDL_SCANCODE_S);
            bool attack = Input::isKeyPressed(SDL_SCANCODE_SPACE);
            if (crouch && !wasCrouch)
                raiseEvent(EventType::Input, EventPriority::Normal, "crouch");
            if (attack && !wasAttack)
                raiseEvent(EventType::Input, EventPriority::Normal, "attack");
            wasCrouch = crouch;
            wasAttack = attack;
        }
        events.dispatch(); // Apply queued input before physics.

        // Update physics
        physics.update(player,deltaTime);

        // Ground check
        float playerLeft = go::getX(player);
        float playerRight = go::getX(player) + go::getWidth(player);
        bool overGap = playerRight > GAP_START && playerLeft < GAP_END;

        if (!overGap && go::getY(player) + go::getHeight(player) >= GROUND_Y && go::getVelocityY(player) >= 0.0f) {
            go::setPosition(player, go::getX(player),GROUND_Y - go::getHeight(player));
            go::setVelocityY(player, 0.0f);
            go::setGrounded(player, true);
        } else {
            go::setGrounded(player, false);
        }

        // Milestone 3 Section B.4: Synchronize moving platforms across peers.
        // Use a shared wall-clock reference instead of each client's startup time.
        auto now = std::chrono::system_clock::now();

        double sharedTimeSeconds = std::chrono::duration<double>(now.time_since_epoch()).count();

        // Repeat the platform movement every 60 seconds.
        // All clients on the same machine use the same time reference.
        double platformTime = std::fmod(sharedTimeSeconds, 60.0);

        // Deterministic oscillation function based on real wall-clock time
        float midPoint = (PLATFORM_MIN_X + PLATFORM_MAX_X) / 2.0f;
        float amplitude = (PLATFORM_MAX_X - PLATFORM_MIN_X) / 2.0f;

        // Sine wave ensures every peer computes the exact same platform position independently
        float newPlatformX = midPoint + amplitude * std::sin(platformTime * 1.5f);
        float newPlatformY = GROUND_Y - 200.0f;

        float platformDeltaX = newPlatformX - previousPlatformX;
        go::setPosition(platform, newPlatformX, newPlatformY);
        previousPlatformX = newPlatformX;

        // Platform collision: stand on top of it and get
        // carried along as it moves.
        bool horizontallyOverPlatform = playerRight > go::getX(platform) && playerLeft < go::getX(platform) + go::getWidth(platform);
        bool landingOnPlatform = go::getY(player) + go::getHeight(player) <= go::getY(platform) + 20.0f &&
            go::getY(player) + go::getHeight(player) >= go::getY(platform) - 20.0f && go::getVelocityY(player) >= 0.0f;

        if (horizontallyOverPlatform && landingOnPlatform) {
            go::setPosition(player, go::getX(player) + platformDeltaX, go::getY(platform) - go::getHeight(player));
            go::setVelocityY(player, 0.0f);
            go::setGrounded(player, true);
        }

        // Detect gameplay conditions and raise events, not direct actions.
        if (go::getY(player) > actualHeight)
            raiseNetworkedEvent(EventType::Death, EventPriority::Critical, "fall",
                                go::getX(player), go::getY(player));

        if (Collision::checkCollision(player, totem))
            raiseEvent(EventType::Collision, EventPriority::Normal,
                       "totem", moveSpeed * deltaTime);

        if (Collision::checkCollision(player, enemySkull))
            raiseEvent(EventType::Collision, EventPriority::High, "enemy");

        // Death/Spawn raised by handlers are handled on the next dispatch.
        // Three passes resolve collision -> death -> spawn in this frame.
        events.dispatch();
        events.dispatch();
        events.dispatch();

        // Share local player position
        {
            std::lock_guard<std::mutex> lock(sharedData.playerMutex);
            sharedData.playerX = go::getX(player);
            sharedData.playerY = go::getY(player);
        }

        // Milestone 3 Section B.4:
        // Deterministic enemy patrol synchronized across P2P clients.
        // Time needed to travel from one patrol boundary to the other
        double patrolDistance = skullPatrolMaxX - skullPatrolMinX;
        double oneWayTime = patrolDistance / skullSpeed;
        double fullCycleTime = 2.0 * oneWayTime;

        // Use the shared wall-clock time, not local deltaTime
        double cycleTime = std::fmod(sharedTimeSeconds, fullCycleTime);

        // Move right, then left, repeating the patrol
        double skullOffset;
        if (cycleTime < oneWayTime) {
            skullOffset = cycleTime * skullSpeed;
        } else {
            skullOffset = (fullCycleTime - cycleTime) * skullSpeed;
        }

        float synchronizedSkullX = skullPatrolMinX + static_cast<float>(skullOffset);

        // Keep the enemy at its existing vertical position
        enemySkull.setPosition(synchronizedSkullX, enemySkull.getY());

        } // end of live simulation (skipped during replay playback)

        // Milestone 4 Section 1.3: record this frame, or move the playback forward.
        // Both use the real-time replay clock, so the replay shows exactly what
        // was on screen (a pause or a 2x game speed is recorded as it looked).
        replay.update(replayDelta);
        events.dispatch(); // handles replay events raised this frame

        // Background color
        SDL_SetRenderDrawColor(renderer, 169, 186, 157, 255);

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
        go::updateAnimation(player);
        enemySkull.updateAnimation(deltaTime);
        totem.updateAnimation(deltaTime);
        portal.updateAnimation(deltaTime);
        go::updateAnimation(platform);

        // Render objects
        portal.render(renderer);
        totem.render(renderer);
        enemySkull.render(renderer);
        go::render(platform, renderer);

        // Milestone 4 Section 1.3: during playback, draw the recorded positions
        // of the other players instead of their live positions.
        if (replay.isPlaying()) {
            for (const ReplayObjectState& remote : replayRemotePlayers) {
                GameObject remotePlayer = makePlayer(remote.id, remote.x, remote.y, go::getWidth(player), go::getHeight(player));
                go::setTexture(remotePlayer, playerTexture);
                go::setSpriteSheet(remotePlayer, 8, 128, 128);
                go::render(remotePlayer, renderer);
            }
        }
        // Render positions received directly from remote peers
        else {
            std::lock_guard<std::mutex> lock(sharedData.playerMutex);

            for (const auto& entry : sharedData.remotePlayers) {
                const RemotePlayerState& remote = entry.second;

                GameObject remotePlayer = makePlayer(entry.first, remote.x, remote.y, go::getWidth(player), go::getHeight(player));
                if (auto* network = remotePlayer.getComponent<NetworkComponent>()) {
                    network->authority = NetworkAuthority::RemoteMirrored;
                }
                go::setTexture(remotePlayer, playerTexture);
                go::setSpriteSheet(remotePlayer, 8, 128, 128);
                go::render(remotePlayer, renderer);
            }
        }

        // Render local player
        go::render(player, renderer);

        // Milestone 4 Section 1.2: Draw messages about events from other clients
        // and drop the ones that have expired.
        {
            const Uint64 nowTicks = SDL_GetTicks();
            notices.erase(std::remove_if(notices.begin(), notices.end(),
                [nowTicks](const Notice& n) { return n.expiresAt <= nowTicks; }), notices.end());

            // Milestone 4 Section 1.3: replay status line
            std::string replayText;
            if (replay.isRecording()) {
                replayText = "REC " + std::to_string(replay.durationMs() / 1000) + "s  (R = stop)";
            } else if (replay.isPlaying()) {
                char buffer[96];
                SDL_snprintf(buffer, sizeof(buffer), "REPLAY %.1fs / %.1fs  speed %.1fx  (V = stop, -/= speed)",
                             replay.playbackPositionMs() / 1000.0, replay.durationMs() / 1000.0, replay.speed());
                replayText = buffer;
            } else {
                replayText = replay.hasRecording() ? "R = record   V = play replay" : "R = record";
            }
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            int outputWidth = 0, outputHeight = 0;
            SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight);
            SDL_RenderDebugText(renderer, 10.0f, outputHeight - 20.0f, replayText.c_str());

            SDL_SetRenderDrawColor(renderer, 200, 30, 30, 255);
            float textY = 10.0f;
            for (const Notice& n : notices) {
                SDL_RenderDebugText(renderer, 10.0f, textY, n.text.c_str());
                textY += 12.0f;
            }
        }
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
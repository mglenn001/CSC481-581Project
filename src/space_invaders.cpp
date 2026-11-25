/* Space Invaders game
 *
 * Three levels with faster and busier invaders.
 * You have limited lives and clear each level by beating all invaders.
 */

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#include "GameObject.h"
#include "Components.h"
#include "GameObjectOps.h"
#include "Physics.h"
#include "Collision.h"
#include "Input.h"
#include "Timeline.h"
#include "EventManager.h"
#include "InputChords.h"   // Milestone 5 Section 2: input chords and sequences

// Basic game numbers
const int   W = 800;             // Screen width
const int   H = 600;             // Screen height
const float PLAYER_SPEED = 320;  // Ship move speed
const float BULLET_SPEED = 520;  // Player shot speed
const float ENEMY_BULLET_SPEED = 260; // Enemy shot speed
const float DROP = 22;           // How far invaders drop at an edge
const float EDGE = 40;           // Space kept from left and right edges

// Milestone 5 Section 2: chord moves
const float DASH_DISTANCE = 170;     // Double-tap A or D: how far the ship dashes
const float DASH_COOLDOWN = 0.6f;    // Seconds before the next dash
const float SHIELD_TIME = 1.5f;      // A + D together: how long the shield lasts
const float SHIELD_COOLDOWN = 5.0f;  // Seconds before the shield can be used again

// Spots on the 1536 x 1024 sprite sheet
const float REF_W = 1536, REF_H = 1024;
const float INV_X[6] = {175, 430, 677, 910, 1140, 1365};   // Invader column centers
const float INV_Y[4] = {165, 378, 590, 818};               // Invader row centers
const float SHIP_X[6] = {145, 398, 650, 895, 1145, 1395};  // Ship column centers
const float SHIP_Y[2] = {320, 680};                        // Ship row centers
const float CROP = 240;                                    // One picture box size

// Settings for each level
struct Level {
    int   rows, cols;
    float speed;            // Invader side speed per second
    float shotGap;          // Average seconds between enemy shots
    int   maxPlayerBullets; // Max shots on screen at once
    int   topRowHp;         // Hits needed to hurt top row
};

const Level LEVELS[3] = {
    {2,  6, 40.f, 1.6f, 3, 1}, // Easy
    {3,  8, 65.f, 1.0f, 2, 1}, // Medium
    {4, 10, 90.f, 0.6f, 2, 2}  // Hard
};

// Invader life stages
enum InvState { INV_ALIVE, INV_DYING, INV_GONE };

// Extra data stored on each invader
struct InvaderComponent : public Component {
    int row = 0;
    int col = 0;
    int hp = 1;
    InvState state = INV_ALIVE;
    float t = 0.0f; // Time spent blowing up
};

// Extra data stored on each bullet
struct BulletComponent : public Component {
    bool fromPlayer = true;
    bool alive = true;
};

// Screens and game states
enum GameState { TITLE, PLAYING, LEVEL_CLEAR, GAME_OVER, WIN };

// Random number from 0.0 to 1.0
float rand01() { return (float)rand() / (float)RAND_MAX; }

// Make a temporary box around a center point for hit checks
GameObject makeHitbox(float cx, float cy, float w, float h) {
    GameObject box("hitbox");
    box.addComponent<TransformComponent>(cx - w * 0.5f, cy - h * 0.5f, w, h);
    box.addComponent<CollidableComponent>();
    return box;
}

// Find the assets folder whether running from build/ or next to the app
std::string resolveAssetPath(const char* relativeAsset) {
    const char* basePath = SDL_GetBasePath();
    if (basePath) {
        std::string base(basePath);

        // Check parent folder when inside build/
        std::string fromBuild = base + "../" + relativeAsset;
        if (SDL_GetPathInfo(fromBuild.c_str(), nullptr)) {
            return fromBuild;
        }

        // Check right next to the app
        std::string nextToExe = base + relativeAsset;
        if (SDL_GetPathInfo(nextToExe.c_str(), nullptr)) {
            return nextToExe;
        }
    }
    return std::string(relativeAsset);
}

// Make one invader object with its parts
GameObject makeInvader(float x, float y, int row, int col, int hp, SDL_Texture* tex) {
    GameObject obj("invader", row * 100 + col);
    obj.addComponent<TransformComponent>(x, y, 64.0f, 56.0f);
    obj.addComponent<VelocityComponent>();
    obj.addComponent<CollidableComponent>();
    auto& r = obj.addComponent<RenderableComponent>();
    r.texture = tex;
    auto& inv = obj.addComponent<InvaderComponent>();
    inv.row = row;
    inv.col = col;
    inv.hp = hp;
    return obj;
}

// Make one bullet object for player or enemy
GameObject makeBullet(float x, float y, float vy, bool fromPlayer) {
    GameObject obj("bullet");
    obj.addComponent<TransformComponent>(x, y, 6.0f, 16.0f);
    auto& v = obj.addComponent<VelocityComponent>();
    v.vy = vy;
    obj.addComponent<CollidableComponent>();
    obj.addComponent<RenderableComponent>();
    auto& b = obj.addComponent<BulletComponent>();
    b.fromPlayer = fromPlayer;
    b.alive = true;
    return obj;
}

struct Game {
    SDL_Window*   window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture*  texInvaders = nullptr;
    SDL_Texture*  texShip = nullptr;
    SDL_Texture*  texBg = nullptr;

    Physics physics{0.0f};   // No gravity in space
    Timeline timeline{1.0};  // Clock in seconds
    EventManager events;       // Event queue and listeners
    GameState state = TITLE;
    int   level = 0;
    int   score = 0;
    int   lives = 3;

    // Player state
    GameObject player{"player", 1};
    float shootCooldown = 0;
    float playerDeadTime = 0;
    bool  playerDead = false;
    float invulnerable = 0;

    // Invader group state
    std::vector<GameObject> inv;
    int   total = 0;
    float dir = 1; // 1 = right, -1 = left
    float shootTimer = 1;
    float animClock = 0;

    // Active shots
    std::vector<GameObject> playerBullets;
    std::vector<GameObject> enemyBullets;

    float stateTimer = 0; // Wait timer between levels

    // Milestone 5 Section 2: input chords
    // The keys come from the same Input class the game already uses for A, D and Space.
    ChordDetector chords{events, [](int key) { return Input::isKeyPressed(static_cast<SDL_Scancode>(key)); }};
    float shieldTime = 0;       // > 0 while the shield is up
    float shieldCooldown = 0;   // > 0 while the shield is recharging
    float dashCooldown = 0;     // > 0 right after a dash
    float dashTrail = 0;        // > 0 while the dash trail is drawn
    float dashFromX = 0;        // where the last dash started

    // Register the chords and what they do
    void setupChords() {
        // A + D pressed together (alone they move left and right)
        chords.addChord("shield", {SDL_SCANCODE_A, SDL_SCANCODE_D}, 120);
        // Double-tap A or D quickly (one tap only moves)
        chords.addSequence("dash-left", {SDL_SCANCODE_A, SDL_SCANCODE_A}, 250);
        chords.addSequence("dash-right", {SDL_SCANCODE_D, SDL_SCANCODE_D}, 250);

        events.subscribe(EventType::InputChord, [this](const GameEvent& e) {
            if (state != PLAYING || playerDead || timeline.isPaused()) return;
            auto* pt = player.getComponent<TransformComponent>();
            if (!pt) return;

            if (e.detail == "shield") {
                if (shieldCooldown > 0) {
                    std::cout << "[t=" << e.timestamp << "] Chord: shield (still recharging)\n";
                    return;
                }
                shieldTime = SHIELD_TIME;
                shieldCooldown = SHIELD_COOLDOWN;
                std::cout << "[t=" << e.timestamp << "] Chord: shield up\n";
            } else if (e.detail == "dash-left" || e.detail == "dash-right") {
                if (dashCooldown > 0) return;
                float direction = (e.detail == "dash-left") ? -1.0f : 1.0f;
                dashFromX = pt->x;
                pt->x = std::max(30.f, std::min((float)W - 30.f, pt->x + direction * DASH_DISTANCE));
                dashCooldown = DASH_COOLDOWN;
                dashTrail = 0.2f;
                std::cout << "[t=" << e.timestamp << "] Chord: " << e.detail << "\n";
            }
        });
    }

    // Print simple messages to the terminal for key events
    void setupEventLogs() {
        events.subscribe(EventType::Input, [](const GameEvent& e) {
            std::cout << "[t=" << e.timestamp << "] Input: " << e.detail << "\n";
        });
        events.subscribe(EventType::Spawn, [](const GameEvent& e) {
            std::cout << "[t=" << e.timestamp << "] Spawn: " << e.detail << "\n";
        });
        events.subscribe(EventType::Collision, [](const GameEvent& e) {
            std::cout << "[t=" << e.timestamp << "] Collision: " << e.detail << "\n";
        });
        events.subscribe(EventType::Death, [](const GameEvent& e) {
            std::cout << "[t=" << e.timestamp << "] Death: " << e.detail << "\n";
        });
    }

    // Queue an event with the current game time
    void emitEvent(EventType type, const std::string& detail,
                   float x = 0.0f, float y = 0.0f,
                   EventPriority prio = EventPriority::Normal,
                   int sourceId = 0, int targetId = 0) {
        GameEvent ev;
        ev.type = type;
        ev.priority = prio;
        ev.timestamp = timeline.getTime();
        ev.sourceId = sourceId;
        ev.targetId = targetId;
        ev.detail = detail;
        ev.x = x;
        ev.y = y;
        events.raise(std::move(ev));
    }

    // Load one image file into an SDL texture
    SDL_Texture* loadTexture(const char* relativeAsset) {
        std::string path = resolveAssetPath(relativeAsset);
        SDL_Texture* tex = IMG_LoadTexture(renderer, path.c_str());
        if (!tex) {
            SDL_Log("Could not load %s (resolved: %s): %s",
                    relativeAsset, path.c_str(), SDL_GetError());
        }
        return tex;
    }

    // Start SDL, window, renderer, textures, and player parts
    bool init() {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            SDL_Log("SDL_Init failed: %s", SDL_GetError());
            return false;
        }

        window = SDL_CreateWindow("Space Invaders", W, H, 0);
        if (!window) {
            SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
            return false;
        }

        renderer = SDL_CreateRenderer(window, nullptr);
        if (!renderer) {
            SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetRenderVSync(renderer, 1);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        texInvaders = loadTexture("assets/invaders.png");
        texShip     = loadTexture("assets/ship.png");
        texBg       = loadTexture("assets/space.jpg");
        if (!texInvaders || !texShip || !texBg) {
            return false;
        }

        // Add parts to the player ship
        player.addComponent<TransformComponent>(W / 2.f, H - 50.f, 64.0f, 64.0f);
        player.addComponent<VelocityComponent>();
        player.addComponent<CollidableComponent>();
        auto& pr = player.addComponent<RenderableComponent>();
        pr.texture = texShip;

        setupEventLogs();
        setupChords();
        timeline.getDeltaTime(); // Clear first frame time
        return true;
    }

    // Clean up textures, window, and SDL
    void cleanup() {
        if (texInvaders) SDL_DestroyTexture(texInvaders);
        if (texShip)     SDL_DestroyTexture(texShip);
        if (texBg)       SDL_DestroyTexture(texBg);
        if (renderer)    SDL_DestroyRenderer(renderer);
        if (window)      SDL_DestroyWindow(window);
        SDL_Quit();
    }

    // Set up invaders and reset positions for level n
    void startLevel(int n) {
        level = n;
        const Level& L = LEVELS[level];
        inv.clear();
        playerBullets.clear();
        enemyBullets.clear();

        float gapX = 56, gapY = 48;
        float startX = W / 2.f - (L.cols - 1) * gapX / 2.f;
        for (int r = 0; r < L.rows; r++) {
            for (int c = 0; c < L.cols; c++) {
                int hp = (r == 0) ? L.topRowHp : 1;
                inv.push_back(makeInvader(startX + c * gapX, 90 + r * gapY,
                                          r, c, hp, texInvaders));
            }
        }
        total = (int)inv.size();
        dir = 1;
        shootTimer = 1.0f;
        go::setPosition(player, W / 2.f, H - 50.f);
        go::setVelocity(player, 0.0f, 0.0f);
        playerDead = false;
        playerDeadTime = 0;
        invulnerable = 1.5f;
        shieldTime = 0;
        shieldCooldown = 0;
        dashCooldown = 0;
        dashTrail = 0;
        chords.reset();
        state = PLAYING;

        // Log new wave and player ship
        emitEvent(EventType::Spawn,
                  "Level " + std::to_string(level + 1) + " invaders", W / 2.f, 90.f);
        emitEvent(EventType::Spawn, "Player ship", W / 2.f, H - 50.f, EventPriority::Normal, player.getId());
    }

    // Start a fresh run from level 1
    void newGame() {
        score = 0;
        lives = 3;
        startLevel(0);
    }

    // Count invaders still alive
    int aliveCount() {
        int n = 0;
        for (auto& obj : inv) {
            auto* ic = obj.getComponent<InvaderComponent>();
            if (ic && ic->state == INV_ALIVE) n++;
        }
        return n;
    }

    // Draw one cropped box from a sprite sheet
    void drawFrame(SDL_Texture* tex, float refCX, float refCY,
                   float cx, float cy, float w, float h) {
        float tw = 0.f, th = 0.f;
        SDL_GetTextureSize(tex, &tw, &th);
        float sx = tw / REF_W, sy = th / REF_H;

        SDL_FRect src{
            refCX * sx - (CROP * sx) * 0.5f,
            refCY * sy - (CROP * sy) * 0.5f,
            CROP * sx,
            CROP * sy
        };
        SDL_FRect dst{cx - w * 0.5f, cy - h * 0.5f, w, h};
        SDL_RenderTexture(renderer, tex, &src, &dst);
    }

    // Draw one invader (walking or blowing up)
    void drawInvader(const GameObject& obj) {
        auto* ic = obj.getComponent<InvaderComponent>();
        auto* rc = obj.getComponent<RenderableComponent>();
        if (!ic || !rc || ic->state == INV_GONE) return;

        int color = ic->row % 4;
        int frame = (ic->state == INV_DYING)
            ? 2 + std::min(3, (int)(ic->t / 0.4f * 4))
            : ((int)(animClock * 2)) % 2;

        drawFrame(rc->texture ? rc->texture : texInvaders,
                  INV_X[frame], INV_Y[color],
                  go::getX(obj), go::getY(obj),
                  go::getWidth(obj), go::getHeight(obj));
    }

    // Draw the ship or its explosion
    void drawPlayer() {
        float px = go::getX(player);
        float py = go::getY(player);
        if (playerDead) {
            int idx = 1 + std::min(10, (int)(playerDeadTime / 1.0f * 11));
            drawFrame(texShip, SHIP_X[idx % 6], SHIP_Y[idx / 6], px, py, 64, 64);
        } else {
            // Blink while safe after respawn
            if (invulnerable > 0 && ((int)(invulnerable * 10)) % 2 == 0) return;
            drawFrame(texShip, SHIP_X[0], SHIP_Y[0], px, py, 64, 64);
        }
    }

    // Handle single key presses like quit, pause, and start
    void handleKeyDown(SDL_Keycode k, bool& running) {
        if (k == SDLK_ESCAPE) running = false;
        if (k == SDLK_P) {
            if (state == PLAYING || state == LEVEL_CLEAR) {
                if (timeline.isPaused()) {
                    timeline.unpause();
                    emitEvent(EventType::Input, "Unpaused game", 0.0f, 0.0f, EventPriority::High);
                } else {
                    timeline.pause();
                    emitEvent(EventType::Input, "Paused game", 0.0f, 0.0f, EventPriority::High);
                }
            }
        }
        if (k == SDLK_RETURN) {
            if (state == TITLE || state == GAME_OVER || state == WIN) {
                if (timeline.isPaused()) timeline.unpause();
                newGame();
            }
        }
    }

    // Move bullets and remove ones that leave the screen
    void updateBullets(std::vector<GameObject>& bullets, float dt, float minY, float maxY) {
        for (auto& b : bullets) {
            auto* bc = b.getComponent<BulletComponent>();
            auto* vc = b.getComponent<VelocityComponent>();
            auto* tc = b.getComponent<TransformComponent>();
            if (!bc || !bc->alive || !vc || !tc) continue;

            tc->x += vc->vx * dt;
            tc->y += vc->vy * dt;
            if (tc->y < minY || tc->y > maxY) bc->alive = false;
        }
        bullets.erase(
            std::remove_if(bullets.begin(), bullets.end(), [](const GameObject& b) {
                auto* bc = b.getComponent<BulletComponent>();
                return !bc || !bc->alive;
            }),
            bullets.end()
        );
    }

    // Step game timers, movement, shooting, hits, and win/lose checks
    void update(float dt) {
        if (timeline.isPaused()) return;

        animClock += dt;

        // Wait a moment after clearing a level
        if (state == LEVEL_CLEAR) {
            stateTimer -= dt;
            if (stateTimer <= 0) {
                if (level >= 2) state = WIN;
                else startLevel(level + 1);
            }
            return;
        }
        if (state != PLAYING) return;

        const Level& L = LEVELS[level];

        // Count down player timers
        if (invulnerable > 0) invulnerable -= dt;
        if (shootCooldown > 0) shootCooldown -= dt;
        if (shieldTime > 0) shieldTime -= dt;
        if (shieldCooldown > 0) shieldCooldown -= dt;
        if (dashCooldown > 0) dashCooldown -= dt;
        if (dashTrail > 0) dashTrail -= dt;

        auto* pt = player.getComponent<TransformComponent>();
        auto* pv = player.getComponent<VelocityComponent>();

        if (playerDead) {
            // Wait through death animation, then respawn
            chords.reset();
            if (pv) { pv->vx = 0.0f; pv->vy = 0.0f; }
            playerDeadTime += dt;
            if (playerDeadTime >= 1.0f) {
                playerDead = false;
                playerDeadTime = 0;
                if (pt) { pt->x = W / 2.f; pt->y = H - 50.f; }
                invulnerable = 2.0f;
                enemyBullets.clear();
                emitEvent(EventType::Spawn, "Player respawned", W / 2.f, H - 50.f,
                          EventPriority::Normal, player.getId());
            }
        } else {
            // Milestone 5 Section 2: look for chords (A + D, double-tap A or D).
            // They are raised as InputChord events and handled after this update.
            chords.update(timeline.getTime(), player.getId());

            // Move left with A and right with D (arrows left for teammate)
            float vx = 0.0f;
            if (Input::isKeyPressed(SDL_SCANCODE_A)) vx -= PLAYER_SPEED;
            if (Input::isKeyPressed(SDL_SCANCODE_D)) vx += PLAYER_SPEED;
            if (pv) { pv->vx = vx; pv->vy = 0.0f; }

            if (player.hasComponent<GravityComponent>()) {
                physics.update(player, dt);
            } else if (pt) {
                pt->x += vx * dt;
            }
            if (pt) {
                pt->x = std::max(30.f, std::min((float)W - 30.f, pt->x));
            }

            // Fire a player shot with Space
            if (Input::isKeyPressed(SDL_SCANCODE_SPACE) && shootCooldown <= 0 &&
                (int)playerBullets.size() < L.maxPlayerBullets && pt) {
                playerBullets.push_back(makeBullet(pt->x, pt->y - 30, -BULLET_SPEED, true));
                shootCooldown = 0.25f;
            }
        }

        // Move invaders sideways and drop down when hitting an edge
        int alive = aliveCount();
        float speed = L.speed * (1.f + 1.5f * (1.f - (float)alive / total));
        bool hitEdge = false;
        for (auto& obj : inv) {
            auto* ic = obj.getComponent<InvaderComponent>();
            auto* tc = obj.getComponent<TransformComponent>();
            if (!ic || !tc || ic->state != INV_ALIVE) continue;
            tc->x += dir * speed * dt;
            if (tc->x < EDGE || tc->x > W - EDGE) hitEdge = true;
        }
        if (hitEdge) {
            dir = -dir;
            for (auto& obj : inv) {
                auto* ic = obj.getComponent<InvaderComponent>();
                auto* tc = obj.getComponent<TransformComponent>();
                if (!ic || !tc || ic->state != INV_ALIVE) continue;
                tc->y += DROP;
                tc->x += dir * speed * dt * 2;
            }
        }

        // Pick a bottom invader in a random column to shoot
        shootTimer -= dt;
        if (shootTimer <= 0 && alive > 0) {
            shootTimer = L.shotGap * (0.5f + rand01());
            GameObject* shooter = nullptr;
            int col = rand() % LEVELS[level].cols;
            for (auto& obj : inv) {
                auto* ic = obj.getComponent<InvaderComponent>();
                if (ic && ic->state == INV_ALIVE && ic->col == col) {
                    if (!shooter || go::getY(obj) > go::getY(*shooter)) shooter = &obj;
                }
            }
            if (!shooter) {
                for (auto& obj : inv) {
                    auto* ic = obj.getComponent<InvaderComponent>();
                    if (ic && ic->state == INV_ALIVE) { shooter = &obj; break; }
                }
            }
            if (shooter) {
                enemyBullets.push_back(
                    makeBullet(go::getX(*shooter), go::getY(*shooter) + 20, ENEMY_BULLET_SPEED, false)
                );
            }
        }

        // Update player and enemy bullets
        updateBullets(playerBullets, dt, -20.f, H + 20.f);
        updateBullets(enemyBullets, dt, -20.f, H + 20.f);

        // Check if player bullets hit invaders
        for (auto& b : playerBullets) {
            auto* bc = b.getComponent<BulletComponent>();
            if (!bc || !bc->alive) continue;
            GameObject bulletBox = makeHitbox(go::getX(b), go::getY(b), 6.f, 16.f);

            for (auto& obj : inv) {
                auto* ic = obj.getComponent<InvaderComponent>();
                if (!ic || ic->state != INV_ALIVE) continue;
                GameObject invBox = makeHitbox(go::getX(obj), go::getY(obj), 40.f, 30.f);

                if (Collision::checkCollision(bulletBox, invBox)) {
                    bc->alive = false;
                    ic->hp--;
                    emitEvent(EventType::Collision,
                              "Player shot hit invader",
                              go::getX(obj), go::getY(obj), EventPriority::Normal,
                              player.getId(), obj.getId());
                    if (ic->hp <= 0) {
                        ic->state = INV_DYING;
                        ic->t = 0;
                        score += 10 * (4 - ic->row);
                        emitEvent(EventType::Death,
                                  "Invader destroyed",
                                  go::getX(obj), go::getY(obj), EventPriority::Normal,
                                  obj.getId());
                    }
                    break;
                }
            }
        }

        // Milestone 5 Section 2: the shield destroys enemy bullets that reach it
        if (!playerDead && shieldTime > 0 && pt) {
            GameObject shieldBox = makeHitbox(pt->x, pt->y, 84.f, 84.f);
            for (auto& b : enemyBullets) {
                auto* bc = b.getComponent<BulletComponent>();
                if (!bc || !bc->alive) continue;
                GameObject bulletBox = makeHitbox(go::getX(b), go::getY(b), 6.f, 16.f);
                if (Collision::checkCollision(bulletBox, shieldBox)) {
                    bc->alive = false;
                    emitEvent(EventType::Collision, "Enemy bullet blocked by shield",
                              pt->x, pt->y, EventPriority::Normal, b.getId(), player.getId());
                }
            }
        }

        // Check if enemy bullets hit the player ship
        if (!playerDead && invulnerable <= 0 && shieldTime <= 0 && pt) {
            GameObject playerBox = makeHitbox(pt->x, pt->y, 40.f, 30.f);
            for (auto& b : enemyBullets) {
                auto* bc = b.getComponent<BulletComponent>();
                if (!bc || !bc->alive) continue;
                GameObject bulletBox = makeHitbox(go::getX(b), go::getY(b), 6.f, 16.f);

                if (Collision::checkCollision(bulletBox, playerBox)) {
                    bc->alive = false;
                    lives--;
                    playerDead = true;
                    playerDeadTime = 0;
                    emitEvent(EventType::Collision, "Enemy bullet hit player ship",
                              pt->x, pt->y, EventPriority::High, b.getId(), player.getId());
                    emitEvent(EventType::Death,
                              "Player ship destroyed (lives left: " + std::to_string(lives) + ")",
                              pt->x, pt->y, EventPriority::Critical, player.getId());
                    break;
                }
            }
        }

        // Finish dying animation after 0.4 seconds
        for (auto& obj : inv) {
            auto* ic = obj.getComponent<InvaderComponent>();
            if (ic && ic->state == INV_DYING) {
                ic->t += dt;
                if (ic->t >= 0.4f) ic->state = INV_GONE;
            }
        }

        // Check if invaders reached the player or lives ran out
        float py = pt ? pt->y : (H - 50.f);
        for (auto& obj : inv) {
            auto* ic = obj.getComponent<InvaderComponent>();
            if (ic && ic->state == INV_ALIVE && go::getY(obj) + 20 >= py - 20) {
                emitEvent(EventType::Death, "Invaders reached player line",
                          go::getX(obj), go::getY(obj), EventPriority::Critical, obj.getId());
                state = GAME_OVER;
                return;
            }
        }
        if (lives <= 0 && !playerDead) {
            state = GAME_OVER;
            return;
        }

        // Move to level clear once all invaders are gone
        if (aliveCount() == 0) {
            state = LEVEL_CLEAR;
            stateTimer = 2.0f;
        }
    }

    // Milestone 5 Section 2: dash trail and shield around the ship
    void drawChordEffects() {
        if (playerDead) return;
        float px = go::getX(player);
        float py = go::getY(player);

        // Dash: fading boxes from where the dash started to where it ended
        if (dashTrail > 0) {
            Uint8 alpha = (Uint8)(200.f * dashTrail / 0.2f);
            SDL_SetRenderDrawColor(renderer, 120, 255, 255, alpha);
            for (int i = 1; i <= 4; i++) {
                float x = dashFromX + (px - dashFromX) * (i / 5.0f);
                SDL_FRect ghost{x - 20.f, py - 12.f, 40.f, 24.f};
                SDL_RenderRect(renderer, &ghost);
            }
        }

        // Shield: two glowing outlines around the ship, blinking near the end
        if (shieldTime > 0) {
            if (shieldTime < 0.4f && ((int)(shieldTime * 20)) % 2 == 0) return;
            SDL_SetRenderDrawColor(renderer, 80, 200, 255, 90);
            SDL_FRect fill{px - 42.f, py - 42.f, 84.f, 84.f};
            SDL_RenderFillRect(renderer, &fill);
            SDL_SetRenderDrawColor(renderer, 120, 230, 255, 255);
            SDL_RenderRect(renderer, &fill);
            SDL_FRect inner{px - 38.f, py - 38.f, 76.f, 76.f};
            SDL_RenderRect(renderer, &inner);
        }
    }

    // Draw one list of bullets as simple rectangles
    void drawBulletList(const std::vector<GameObject>& bullets) {
        for (const auto& b : bullets) {
            auto* bc = b.getComponent<BulletComponent>();
            if (!bc || !bc->alive) continue;
            SDL_FRect r{go::getX(b) - 3.f, go::getY(b) - 8.f, 6.f, 16.f};
            SDL_RenderFillRect(renderer, &r);
        }
    }

    // Draw the background, ships, invaders, shots, and window title
    void render() {
        SDL_RenderTexture(renderer, texBg, nullptr, nullptr);

        if (state == PLAYING || state == LEVEL_CLEAR) {
            for (const auto& obj : inv) drawInvader(obj);
            drawChordEffects();
            drawPlayer();

            SDL_SetRenderDrawColor(renderer, 120, 255, 255, 255);
            drawBulletList(playerBullets);

            SDL_SetRenderDrawColor(renderer, 255, 160, 40, 255);
            drawBulletList(enemyBullets);
        }

        // Sample invaders on the title screen
        if (state == TITLE) {
            for (int c = 0; c < 4; c++) {
                drawFrame(texInvaders, INV_X[((int)(animClock * 2)) % 2], INV_Y[c],
                          W / 2.f - 135 + c * 90, H / 2.f, 64, 56);
            }
        }

        SDL_RenderPresent(renderer);
        updateTitleText();
    }

    // Show score, level, and status in the window title bar
    void updateTitleText() {
        char t[200];
        const char* names[3] = {"Easy", "Medium", "Hard"};
        const char* pausedSuffix = timeline.isPaused() ? "  [PAUSED - Press P]" : "";
        switch (state) {
            case TITLE:
                snprintf(t, sizeof(t), "SPACE INVADERS - ENTER to start  |  A+D shield  |  double-tap A/D dash"); break;
            case PLAYING: {
                char shield[32];
                if (shieldTime > 0) snprintf(shield, sizeof(shield), "UP");
                else if (shieldCooldown > 0) snprintf(shield, sizeof(shield), "%.1fs", shieldCooldown);
                else snprintf(shield, sizeof(shield), "READY (A+D)");
                snprintf(t, sizeof(t), "Level %d (%s)  |  Score: %d  |  Lives: %d  |  Shield: %s%s",
                         level + 1, names[level], score, lives, shield, pausedSuffix); break;
            }
            case LEVEL_CLEAR:
                snprintf(t, sizeof(t), "Level %d clear!  Score: %d%s",
                         level + 1, score, pausedSuffix); break;
            case GAME_OVER:
                snprintf(t, sizeof(t), "GAME OVER  |  Score: %d  |  Press ENTER to retry", score); break;
            case WIN:
                snprintf(t, sizeof(t), "YOU WIN!  |  Score: %d  |  Press ENTER to play again", score); break;
        }
        SDL_SetWindowTitle(window, t);
    }
};

// Run the main event, update, event-dispatch, and draw loop
int main(int, char**) {
    srand((unsigned)time(nullptr));
    Game game;
    if (!game.init()) { game.cleanup(); return 1; }

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                game.handleKeyDown(e.key.key, running);
            }
        }

        // Cap big frame jumps so things don't teleport
        float dt = static_cast<float>(game.timeline.getDeltaTime());
        if (dt > 0.05f) dt = 0.05f;

        game.update(dt);
        game.events.dispatch();
        game.render();
    }
    game.cleanup();
    return 0;
}
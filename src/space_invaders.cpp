/* Space Invaders game
 *
 * The game features three levels of increasing difficulty, with invaders moving faster and shooting more frequently.
 * The player has a limited number of lives and must clear all invaders to progress to the next level.
 */

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

// ---------- Settings ----------
const int   W = 800;             // window width
const int   H = 600;             // window height
const float PLAYER_SPEED = 320;  // ship speed
const float BULLET_SPEED = 520;  // player bullet speed
const float ENEMY_BULLET_SPEED = 260;
const float DROP = 22;           // how far invaders move down at the edge
const float EDGE = 40;           // distance from the screen edge

// Sprite sheet layout (measured on a 1536 x 1024 picture).
// These are the center of each frame. Change them if the sprites look cut off.
const float REF_W = 1536, REF_H = 1024;
const float INV_X[6] = {175, 430, 677, 910, 1140, 1365};   // invaders: column centers
const float INV_Y[4] = {165, 378, 590, 818};               // invaders: row centers
const float SHIP_X[6] = {145, 398, 650, 895, 1145, 1395};  // ship: column centers
const float SHIP_Y[2] = {320, 680};                        // ship: row centers
const float CROP = 240;                                    // size of one frame in the picture

// Level settings
struct Level {
    int   rows, cols;
    float speed; // invader speed (pixels per second)
    float shotGap; // average seconds between enemy shots
    int   maxPlayerBullets;
    int   topRowHp; // hits needed for the top row
};

const Level LEVELS[3] = {
    {2,  6, 40.f, 1.6f, 3, 1}, // easy
    {3,  8, 65.f, 1.0f, 2, 1}, // medium
    {4, 10, 90.f, 0.6f, 2, 2}  // hard
};

// Game objects
enum InvState { INV_ALIVE, INV_DYING, INV_GONE };

struct Invader {
    float x, y; // center
    int row, col;
    int hp;
    InvState state;
    float t; // time spent dying
};

struct Bullet {
    float x, y, vy;
    bool alive;
};

enum GameState { TITLE, PLAYING, LEVEL_CLEAR, GAME_OVER, WIN };

// Check if two boxes touch (boxes use center x,y and width,height)
bool overlap(float ax, float ay, float aw, float ah,
             float bx, float by, float bw, float bh) {
    return fabsf(ax - bx) * 2 < (aw + bw) && fabsf(ay - by) * 2 < (ah + bh);
}

float rand01() { return (float)rand() / (float)RAND_MAX; }

// Resolve an asset path relative to the executable location so running inside
// the ignored "build" folder finds the top-level "assets" folder reliably.
std::string resolveAssetPath(const char* relativeAsset) {
    const char* basePath = SDL_GetBasePath();
    if (basePath) {
        std::string base(basePath);

        // Case 1: binary is in <project_root>/build/ -> assets is at <base>../assets/...
        std::string fromBuild = base + "../" + relativeAsset;
        if (SDL_GetPathInfo(fromBuild.c_str(), nullptr)) {
            return fromBuild;
        }

        // Case 2: assets sits right next to the executable
        std::string nextToExe = base + relativeAsset;
        if (SDL_GetPathInfo(nextToExe.c_str(), nullptr)) {
            return nextToExe;
        }
    }
    return std::string(relativeAsset);
}

// The game
struct Game {
    SDL_Window*   window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture*  texInvaders = nullptr;
    SDL_Texture*  texShip = nullptr;
    SDL_Texture*  texBg = nullptr;

    GameState state = TITLE;
    int   level = 0; // 0, 1, 2
    int   score = 0;
    int   lives = 3;

    // Player
    float px = W / 2.f, py = H - 50.f;
    float shootCooldown = 0;
    float playerDeadTime = 0; // > 0 while the ship is exploding
    bool  playerDead = false;
    float invulnerable = 0; // short safe time after respawn

    // Invaders
    std::vector<Invader> inv;
    int   total = 0;
    float dir = 1; // 1 = right, -1 = left
    float shootTimer = 1;
    float animClock = 0; // for the 2-frame walk animation

    std::vector<Bullet> playerBullets;
    std::vector<Bullet> enemyBullets;

    float stateTimer = 0; // used by LEVEL_CLEAR

    SDL_Texture* loadTexture(const char* relativeAsset) {
        std::string path = resolveAssetPath(relativeAsset);
        SDL_Texture* tex = IMG_LoadTexture(renderer, path.c_str());
        if (!tex) {
            SDL_Log("Could not load %s (resolved: %s): %s",
                    relativeAsset, path.c_str(), SDL_GetError());
        }
        return tex;
    }

    // Start up
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
        return true;
    }

    void cleanup() {
        if (texInvaders) SDL_DestroyTexture(texInvaders);
        if (texShip)     SDL_DestroyTexture(texShip);
        if (texBg)       SDL_DestroyTexture(texBg);
        if (renderer)    SDL_DestroyRenderer(renderer);
        if (window)      SDL_DestroyWindow(window);
        SDL_Quit();
    }

    // Set up a level
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
                Invader i;
                i.x = startX + c * gapX;
                i.y = 90 + r * gapY;
                i.row = r;
                i.col = c;
                i.hp = (r == 0) ? L.topRowHp : 1; // top row can be tougher
                i.state = INV_ALIVE;
                i.t = 0;
                inv.push_back(i);
            }
        }
        total = (int)inv.size();
        dir = 1;
        shootTimer = 1.0f;
        px = W / 2.f;
        playerDead = false;
        playerDeadTime = 0;
        invulnerable = 1.5f;
        state = PLAYING;
    }

    void newGame() {
        score = 0;
        lives = 3;
        startLevel(0);
    }

    int aliveCount() {
        int n = 0;
        for (auto& i : inv) if (i.state == INV_ALIVE) n++;
        return n;
    }

    // Draw one frame from a sprite sheet
    // cx, cy = where the center of the sprite goes. w, h = size on screen.
    void drawFrame(SDL_Texture* tex, float refCX, float refCY,
                   float cx, float cy, float w, float h, bool flipHint = false) {
        (void)flipHint;
        float tw = 0.f, th = 0.f;
        SDL_GetTextureSize(tex, &tw, &th);
        float sx = tw / REF_W, sy = th / REF_H; // in case the picture size is different

        SDL_FRect src;
        src.w = CROP * sx;
        src.h = CROP * sy;
        src.x = refCX * sx - src.w * 0.5f;
        src.y = refCY * sy - src.h * 0.5f;

        SDL_FRect dst{cx - w * 0.5f, cy - h * 0.5f, w, h};
        SDL_RenderTexture(renderer, tex, &src, &dst);
    }

    void drawInvader(const Invader& i) {
        int color = i.row % 4; // each row has its own color
        int frame;
        if (i.state == INV_DYING) {
            frame = 2 + std::min(3, (int)(i.t / 0.4f * 4)); // explosion frames 2 to 5
        } else {
            frame = ((int)(animClock * 2)) % 2; // walk frames 0 and 1
        }
        drawFrame(texInvaders, INV_X[frame], INV_Y[color], i.x, i.y, 64, 56);
    }

    void drawPlayer() {
        if (playerDead) {
            // ship explosion uses frames 1 to 11
            int idx = 1 + std::min(10, (int)(playerDeadTime / 1.0f * 11));
            drawFrame(texShip, SHIP_X[idx % 6], SHIP_Y[idx / 6], px, py, 64, 64);
        } else {
            // blink while safe
            if (invulnerable > 0 && ((int)(invulnerable * 10)) % 2 == 0) return;
            drawFrame(texShip, SHIP_X[0], SHIP_Y[0], px, py, 64, 64);
        }
    }

    // Input
    void handleKeyDown(SDL_Keycode k, bool& running) {
        if (k == SDLK_ESCAPE) running = false;
        if (k == SDLK_RETURN) {
            if (state == TITLE || state == GAME_OVER || state == WIN) newGame();
        }
    }

    // Update
    void update(float dt) {
        animClock += dt;

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
        const bool* keys = SDL_GetKeyboardState(nullptr);

        // Player
        if (invulnerable > 0) invulnerable -= dt;
        if (shootCooldown > 0) shootCooldown -= dt;

        if (playerDead) {
            playerDeadTime += dt;
            if (playerDeadTime >= 1.0f) {
                playerDead = false;
                playerDeadTime = 0;
                px = W / 2.f;
                invulnerable = 2.0f;
                enemyBullets.clear();
            }
        } else {
            if (keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A]) px -= PLAYER_SPEED * dt;
            if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) px += PLAYER_SPEED * dt;
            px = std::max(30.f, std::min((float)W - 30.f, px));

            if (keys[SDL_SCANCODE_SPACE] && shootCooldown <= 0 &&
                (int)playerBullets.size() < L.maxPlayerBullets) {
                playerBullets.push_back({px, py - 30, -BULLET_SPEED, true});
                shootCooldown = 0.25f;
            }
        }

        // Move invaders
        int alive = aliveCount();
        float speed = L.speed * (1.f + 1.5f * (1.f - (float)alive / total)); // faster when fewer
        bool hitEdge = false;
        for (auto& i : inv) {
            if (i.state != INV_ALIVE) continue;
            i.x += dir * speed * dt;
            if (i.x < EDGE || i.x > W - EDGE) hitEdge = true;
        }
        if (hitEdge) {
            dir = -dir;
            for (auto& i : inv) {
                if (i.state != INV_ALIVE) continue;
                i.y += DROP;
                i.x += dir * speed * dt * 2;   // step away from the edge
            }
        }

        // Invaders shoot
        shootTimer -= dt;
        if (shootTimer <= 0 && alive > 0) {
            shootTimer = L.shotGap * (0.5f + rand01());
            // pick a random column, then use the lowest invader in it
            Invader* shooter = nullptr;
            int col = rand() % LEVELS[level].cols;
            for (auto& i : inv) {
                if (i.state == INV_ALIVE && i.col == col)
                    if (!shooter || i.y > shooter->y) shooter = &i;
            }
            if (!shooter) {   // that column is empty, so pick any alive invader
                for (auto& i : inv) if (i.state == INV_ALIVE) { shooter = &i; break; }
            }
            if (shooter) enemyBullets.push_back({shooter->x, shooter->y + 20, ENEMY_BULLET_SPEED, true});
        }

        // Move bullets
        for (auto& b : playerBullets) {
            b.y += b.vy * dt;
            if (b.y < -20) b.alive = false;
        }
        for (auto& b : enemyBullets) {
            b.y += b.vy * dt;
            if (b.y > H + 20) b.alive = false;
        }

        // Player bullet hits invader
        for (auto& b : playerBullets) {
            if (!b.alive) continue;
            for (auto& i : inv) {
                if (i.state != INV_ALIVE) continue;
                if (overlap(b.x, b.y, 6, 16, i.x, i.y, 40, 30)) {
                    b.alive = false;
                    i.hp--;
                    if (i.hp <= 0) {
                        i.state = INV_DYING;
                        i.t = 0;
                        score += 10 * (4 - i.row);   // top rows give more points
                    }
                    break;
                }
            }
        }

        // Enemy bullet hits player
        if (!playerDead && invulnerable <= 0) {
            for (auto& b : enemyBullets) {
                if (b.alive && overlap(b.x, b.y, 6, 16, px, py, 40, 30)) {
                    b.alive = false;
                    lives--;
                    playerDead = true;
                    playerDeadTime = 0;
                    break;
                }
            }
        }

        // Clean up dead bullets
        playerBullets.erase(std::remove_if(playerBullets.begin(), playerBullets.end(),
                            [](const Bullet& b) { return !b.alive; }), playerBullets.end());
        enemyBullets.erase(std::remove_if(enemyBullets.begin(), enemyBullets.end(),
                           [](const Bullet& b) { return !b.alive; }), enemyBullets.end());

        // Dying invaders
        for (auto& i : inv) {
            if (i.state == INV_DYING) {
                i.t += dt;
                if (i.t >= 0.4f) i.state = INV_GONE;
            }
        }

        // Lose: invaders reach the ship
        for (auto& i : inv) {
            if (i.state == INV_ALIVE && i.y + 20 >= py - 20) {
                state = GAME_OVER;
                return;
            }
        }
        if (lives <= 0 && !playerDead) {
            state = GAME_OVER;
            return;
        }

        // win the level
        if (aliveCount() == 0) {
            state = LEVEL_CLEAR;
            stateTimer = 2.0f;
        }
    }

    // Draw
    void render() {
        SDL_RenderTexture(renderer, texBg, nullptr, nullptr); // background

        if (state == PLAYING || state == LEVEL_CLEAR) {
            for (auto& i : inv)
                if (i.state != INV_GONE) drawInvader(i);
            drawPlayer();

            SDL_SetRenderDrawColor(renderer, 120, 255, 255, 255); // player bullets
            for (auto& b : playerBullets) {
                SDL_FRect r{b.x - 3.f, b.y - 8.f, 6.f, 16.f};
                SDL_RenderFillRect(renderer, &r);
            }
            SDL_SetRenderDrawColor(renderer, 255, 160, 40, 255); // enemy bullets
            for (auto& b : enemyBullets) {
                SDL_FRect r{b.x - 3.f, b.y - 8.f, 6.f, 16.f};
                SDL_RenderFillRect(renderer, &r);
            }
        }

        // Show a few invaders on the title screen so it is not empty
        if (state == TITLE) {
            for (int c = 0; c < 4; c++)
                drawFrame(texInvaders, INV_X[((int)(animClock * 2)) % 2], INV_Y[c],
                          W / 2.f - 135 + c * 90, H / 2.f, 64, 56);
        }

        SDL_RenderPresent(renderer);
        updateTitleText();
    }

    // No font library yet, so we show text in the window title.
    void updateTitleText() {
        char t[200];
        const char* names[3] = {"Easy", "Medium", "Hard"};
        switch (state) {
            case TITLE:
                snprintf(t, sizeof(t), "SPACE INVADERS - Press ENTER to start"); break;
            case PLAYING:
                snprintf(t, sizeof(t), "Level %d (%s)  |  Score: %d  |  Lives: %d",
                         level + 1, names[level], score, lives); break;
            case LEVEL_CLEAR:
                snprintf(t, sizeof(t), "Level %d clear!  Score: %d", level + 1, score); break;
            case GAME_OVER:
                snprintf(t, sizeof(t), "GAME OVER  |  Score: %d  |  Press ENTER to retry", score); break;
            case WIN:
                snprintf(t, sizeof(t), "YOU WIN!  |  Score: %d  |  Press ENTER to play again", score); break;
        }
        SDL_SetWindowTitle(window, t);
    }
};

// Main loop
int main(int, char**) {
    srand((unsigned)time(nullptr));
    Game game;
    if (!game.init()) { game.cleanup(); return 1; }

    bool running = true;
    Uint64 last = SDL_GetTicks();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                game.handleKeyDown(e.key.key, running);
            }
        }
        Uint64 now = SDL_GetTicks();
        float dt = (now - last) / 1000.f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;   // avoid big jumps

        game.update(dt);
        game.render();
    }
    game.cleanup();
    return 0;
}
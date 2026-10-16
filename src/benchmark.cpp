/*
 * Milestone 3 Sections C.2-C.4: headless performance benchmark.
 * Usage (normally started for you by run_experiments.py):
 *    ./benchmark <clientID> <numClients> <text|binary> <movingObjects> <staticObjects>
 *                <iterations> <startEpochMs> [networkTickMicros]
 */
#include "Networking.h"
#include "SharedData.h"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// Size of the play area and the player box, similar to the real game
static const float WORLD_WIDTH = 1900.0f;
static const float WORLD_HEIGHT = 900.0f;
static const float BOX_SIZE = 64.0f;

// A moving object: it slides left/right and bounces off the world edges
struct MovingObject {
    float x, y, speedX;
};

// A static object: a fixed box (like a platform)
struct StaticObject {
    float x, y;
};

// True if two BOX_SIZE squares overlap (simple AABB collision test)
static bool overlaps(float ax, float ay, float bx, float by) {
    return ax < bx + BOX_SIZE && ax + BOX_SIZE > bx &&
           ay < by + BOX_SIZE && ay + BOX_SIZE > by;
}

int main(int argc, char* argv[]) {
    if (argc < 8) {
        std::cerr << "Usage: ./benchmark <clientID> <numClients> <text|binary> <movingObjects> "
                     "<staticObjects> <iterations> <startEpochMs> [networkTickMicros]" << std::endl;
        return 1;
    }

    const int clientID = std::stoi(argv[1]);
    const int numClients = std::stoi(argv[2]);
    const NetworkFormat format = NetFormat::parseFormat(argv[3]);
    const int movingCount = std::stoi(argv[4]);
    const int staticCount = std::stoi(argv[5]);
    const long long iterations = std::stoll(argv[6]);
    const long long startEpochMs = std::stoll(argv[7]);
    const int tickMicros = (argc >= 9) ? std::stoi(argv[8]) : 1000;

    /* Build the scene */
    // Positions are computed from the object index so every run is identical
    std::vector<MovingObject> movingObjects(movingCount);
    for (int i = 0; i < movingCount; i++) {
        movingObjects[i] = { float((i * 37) % 1800), float((i * 53) % 850), 20.0f + float(i % 7) * 15.0f };
    }
    std::vector<StaticObject> staticObjects(staticCount);
    for (int i = 0; i < staticCount; i++) {
        staticObjects[i] = { float((i * 71) % 1800), float((i * 29) % 850) };
    }
    float playerX = 100.0f * clientID;
    float playerY = 400.0f;
    float playerSpeed = 120.0f;

    // Start the engine's real networking thread
    SharedData sharedData;
    sharedData.networkFormat = format;
    sharedData.networkTickMicros = tickMicros;
    // Start with the real object positions (not zeros) so the very first messages
    // already have realistic size and cost.
    sharedData.localObjects.resize(movingCount);
    for (int i = 0; i < movingCount; i++) {
        sharedData.localObjects[i] = { movingObjects[i].x, movingObjects[i].y };
    }
    sharedData.playerX = playerX;
    sharedData.playerY = playerY;
    std::thread network(networkingThread, std::ref(sharedData), clientID);

    // Wait for the common start time so all clients begin together
    auto nowMs = []() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    };
    while (nowMs() < startEpochMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Wait (up to 10 s) until every other client is visible, so the data really flows
    const auto waitStart = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - waitStart < std::chrono::seconds(10)) {
        {
            std::lock_guard<std::mutex> lock(sharedData.playerMutex);
            if (static_cast<int>(sharedData.remotePlayers.size()) >= numClients - 1) { break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // The measured game loop
    const float dt = 1.0f / 60.0f; // fixed time step, same work every iteration
    double checksum = 0.0; // keeps the compiler from removing the work
    uint64_t collisions = 0;

    // Reset the network counters so they only describe the measured loop,
    // not the waiting time before it.
    sharedData.stats.ticks = 0;
    sharedData.stats.bytesSent = 0;
    sharedData.stats.encodeNanos = 0;
    sharedData.stats.messagesReceived = 0;
    sharedData.stats.decodeNanos = 0;

    const auto loopStart = std::chrono::steady_clock::now();

    for (long long iter = 0; iter < iterations; iter++) {

        // Move the local player (bounces between the world edges)
        playerX += playerSpeed * dt;
        if (playerX < 0.0f || playerX > WORLD_WIDTH) { playerSpeed = -playerSpeed; }

        // Move every moving object
        for (int i = 0; i < movingCount; i++) {
            MovingObject& o = movingObjects[i];
            o.x += o.speedX * dt;
            if (o.x < 0.0f || o.x > WORLD_WIDTH) { o.speedX = -o.speedX; }
        }

        // Collision checks: player against every static and moving object
        for (int i = 0; i < staticCount; i++) {
            if (overlaps(playerX, playerY, staticObjects[i].x, staticObjects[i].y)) { collisions++; }
        }
        for (int i = 0; i < movingCount; i++) {
            if (overlaps(playerX, playerY, movingObjects[i].x, movingObjects[i].y)) { collisions++; }
        }

        // Share state with the networking thread
        {
            std::lock_guard<std::mutex> lock(sharedData.playerMutex);

            // Publish our state so the network thread can send it
            sharedData.playerX = playerX;
            sharedData.playerY = playerY;
            for (int i = 0; i < movingCount; i++) {
                sharedData.localObjects[i] = { movingObjects[i].x, movingObjects[i].y };
            }

            // "Render" what the other clients sent us
            for (const auto& peer : sharedData.remotePlayers) {
                checksum += peer.second.x + peer.second.y;
            }
            for (const auto& peer : sharedData.remoteObjects) {
                for (const ObjectState& obj : peer.second) { checksum += obj.x + obj.y; }
            }
        }
    }

    const auto loopEnd = std::chrono::steady_clock::now();
    const double elapsedSeconds = std::chrono::duration<double>(loopEnd - loopStart).count();

    // Collect network statistics, then shut down
    const uint64_t ticks = sharedData.stats.ticks.load();
    const uint64_t bytesSent = sharedData.stats.bytesSent.load();
    const double encodeMicrosPerTick = ticks ? sharedData.stats.encodeNanos.load() / 1000.0 / ticks : 0.0;
    const uint64_t received = sharedData.stats.messagesReceived.load();
    const double decodeMicrosPerMessage = received ? sharedData.stats.decodeNanos.load() / 1000.0 / received : 0.0;
    const double bytesPerTick = ticks ? double(bytesSent) / ticks : 0.0;

    // Short pause so slower peers still see our data while they finish their loops
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    sharedData.running = false;
    network.join();

    // One machine-readable line, read by run_experiments.py
    std::cout << "RESULT,"
              << clientID << "," << NetFormat::formatName(format) << "," << numClients << ","
              << movingCount << "," << staticCount << "," << iterations << ","
              << elapsedSeconds << "," << ticks << "," << bytesPerTick << ","
              << encodeMicrosPerTick << "," << decodeMicrosPerMessage << ","
              << collisions << "," << checksum << std::endl;
    return 0;
}
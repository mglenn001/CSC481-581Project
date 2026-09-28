#pragma once

#include <mutex>
#include <unordered_map>
#include <atomic>

// Structure holding location data for remote peers
struct RemotePlayerState {
    float x;
    float y;
};

struct SharedData {
    // Controls when worker threads should stop
    std::atomic<bool> running{true};

    // Local player's latest position
    float playerX = 0.0f;
    float playerY = 0.0f;

    // Current timeline/game speed for this client
    // 0.5 = half speed
    // 1.0 = normal speed
    // 2.0 = double speed
    std::atomic<double> timeScale{1.0};

    // Positions received from the server
    std::unordered_map<int, RemotePlayerState> remotePlayers;

    // Protect player positions shared between threads
    std::mutex playerMutex;
};
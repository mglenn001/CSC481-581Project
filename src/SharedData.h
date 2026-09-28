#pragma once

#include <mutex>
#include <unordered_map>
#include <atomic>

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

    // Server-authoritative moving platform position.
    // This is set only by data received over the network
    // (reserved ID 0 in the reply string), never computed
    // locally and never tied to this client's own Timeline.
    float platformX = 0.0f;
    float platformY = 0.0f;

    // Protect player positions shared between threads
    std::mutex playerMutex;
};
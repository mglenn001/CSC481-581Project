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

    // Positions received from the server
    std::unordered_map<int, RemotePlayerState> remotePlayers;

    // Protect player positions shared between threads
    std::mutex playerMutex;
};
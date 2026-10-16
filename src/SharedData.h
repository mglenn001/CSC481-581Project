#pragma once

#include <mutex>
#include <unordered_map>
#include <atomic>
#include <chrono>

// Milestone 3 Section C.1: which data format this client sends (TEXT or BINARY)
#include "NetworkFormat.h"

// Structure holding location data for remote peers
struct RemotePlayerState {
    float x;
    float y;
        std::chrono::steady_clock::time_point lastSeen = std::chrono::steady_clock::now();
};

struct SharedData {
    // Controls when worker threads should stop
    std::atomic<bool> running{true};

    // Milestone 3 Section C.1: data format used to send player positions.
    // Set once in main() before the networking thread starts. Default = TEXT.
    NetworkFormat networkFormat = NetworkFormat::TEXT;

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
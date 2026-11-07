/*
 * Milestone 4 Section 1.3: headless test of the replay system.
 * No SDL or networking needed. A fake "player" moves 1 pixel per 10 ms.
 */
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "../src/EventManager.h"
#include "../src/NetworkEvent.h"
#include "../src/ReplaySystem.h"

static int failures = 0;
#define CHECK(cond) do { if (cond) { std::cout << "  ok   " #cond "\n"; } \
    else { std::cout << "  FAIL " #cond "  (line " << __LINE__ << ")\n"; ++failures; } } while (0)

int main() {
    EventManager events;
    int64_t fakeClock = 0;
    ReplaySystem replay(events, [&]() { return fakeClock; }, 5000);

    // The "game": one object (id 1) whose x the replay records and sets
    float playerX = 0.0f;
    std::vector<float> shownX;          // every x the replay applied
    std::vector<std::string> replayed;  // events given back during playback
    std::vector<std::string> status;

    replay.setSnapshotSource([&]() {
        return std::vector<ReplayObjectState>{{1, playerX, 0.0f, 0.0f, 0.0f}};
    });
    replay.setApplyFrame([&](const ReplayFrame& f) {
        for (const auto& o : f.objects) if (o.id == 1) { playerX = o.x; shownX.push_back(o.x); }
    });
    replay.setEventPlayback([&](const GameEvent& e) { replayed.push_back(e.detail); });
    replay.setStatusCallback([&](const std::string& s) { status.push_back(s); });

    auto frame = [&](double dt) {   // one game loop
        replay.update(dt);
        events.dispatch();
    };

    std::cout << "1. Playback with nothing recorded\n";
    replay.requestPlaybackStart();
    events.dispatch();
    CHECK(!replay.isPlaying());
    CHECK(!status.empty() && status.back() == "Replay: nothing recorded yet");

    std::cout << "2. Events before recording are not recorded\n";
    events.raise(GameEvent{EventType::Death, EventPriority::Critical, 0, 1, 1, "before", 0, 0});
    events.dispatch();

    std::cout << "3. Record 1 second, with a death at 500 ms\n";
    replay.requestRecordStart();
    events.dispatch();
    CHECK(replay.isRecording());
    for (int i = 1; i <= 100; ++i) {      // 100 frames of 10 ms
        playerX = static_cast<float>(i);
        if (i == 50) events.raise(GameEvent{EventType::Death, EventPriority::Critical, 0, 1, 1, "enemy", 0, 0});
        if (i == 51) events.raise(GameEvent{EventType::Spawn, EventPriority::High, 0, 1, 1, "portal", 0, 0});
        events.dispatch();                // game handles its events, replay records them
        frame(0.010);
    }
    replay.requestRecordStop();
    events.dispatch();
    CHECK(!replay.isRecording());
    CHECK(replay.frameCount() == 101);    // frame 0 + 100
    CHECK(replay.eventCount() == 2);      // "before" was not recorded
    CHECK(std::llabs(replay.durationMs() - 1000) <= 1);

    std::cout << "4. Play back at 1x and check it ends and restores the live game\n";
    playerX = 777.0f;                     // where the live game is now
    shownX.clear();
    replay.requestPlaybackStart(1.0f);
    events.dispatch();
    CHECK(replay.isPlaying());
    CHECK(!shownX.empty() && shownX.front() == 0.0f);   // starts at the first frame
    int loops = 0;
    while (replay.isPlaying() && loops < 1000) { frame(0.010); ++loops; }
    CHECK(!replay.isPlaying());
    CHECK(loops >= 100 && loops <= 102);  // about 1 second of 10 ms frames
    bool increasing = true;
    for (size_t i = 1; i + 1 < shownX.size(); ++i) if (shownX[i] < shownX[i - 1]) increasing = false;
    CHECK(increasing);                    // frames shown in order
    CHECK(shownX.size() >= 2 && shownX[shownX.size() - 2] == 100.0f); // last recorded frame shown
    CHECK(playerX == 777.0f);             // live game put back afterwards
    CHECK(replayed.size() == 2 && replayed[0] == "enemy" && replayed[1] == "portal");
    CHECK(replay.eventCount() == 2);      // playback did not record anything

    std::cout << "5. Play back at 2x takes half as many frames\n";
    replay.requestPlaybackStart(2.0f);
    events.dispatch();
    loops = 0;
    while (replay.isPlaying() && loops < 1000) { frame(0.010); ++loops; }
    CHECK(loops >= 50 && loops <= 52);

    std::cout << "6. Speed event during playback, and manual stop\n";
    replay.requestPlaybackStart(1.0f);
    events.dispatch();
    replay.requestSpeed(0.5f);
    events.dispatch();
    CHECK(replay.speed() == 0.5f);
    for (int i = 0; i < 20; ++i) frame(0.010);   // 200 ms real = 100 ms of replay
    CHECK(std::llabs(replay.playbackPositionMs() - 100) <= 2);
    replay.requestPlaybackStop();
    events.dispatch();
    CHECK(!replay.isPlaying());
    CHECK(playerX == 777.0f);

    std::cout << "7. Recording stops by itself at the maximum length (5 s here)\n";
    replay.requestRecordStart();
    events.dispatch();
    for (int i = 0; i < 700 && replay.isRecording(); ++i) frame(0.010);
    CHECK(!replay.isRecording());
    CHECK(replay.durationMs() >= 5000 && replay.durationMs() <= 5010);

    std::cout << "8. Starting playback while recording finishes the recording first\n";
    replay.requestRecordStart();
    events.dispatch();
    for (int i = 0; i < 10; ++i) frame(0.010);
    replay.requestPlaybackStart();
    events.dispatch();
    CHECK(replay.isPlaying());
    CHECK(replay.frameCount() == 11);
    replay.requestPlaybackStop();
    events.dispatch();

    std::cout << "9. Replay events stay on this client (network decoder rejects them)\n";
    GameEvent control{EventType::ReplayPlaybackStart, EventPriority::High, 0, 1, 1, "replay", 1, 0};
    std::string wire = NetEvent::encode(control);
    GameEvent decoded;
    CHECK(!NetEvent::decode(wire.data(), wire.size(), decoded));

    std::cout << (failures == 0 ? "ALL REPLAY TESTS PASSED\n" : "REPLAY TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}

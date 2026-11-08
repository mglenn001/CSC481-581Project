#pragma once

/*
 * Milestone 4 Section 1.3: Replay System
 *
 * The replay system is built on the event manager. Everything it does is
 * started by an event:
 *
 *   ReplayRecordStart    begin a new recording
 *   ReplayRecordStop     finish the recording
 *   ReplayPlaybackStart  play the recording back (x = speed, 0 means 1.0x)
 *   ReplayPlaybackStop   stop playback and return to the live game
 *   ReplayPlaybackSpeed  change the playback speed (x = new speed)
 *
 * What is recorded:
 *   1. Frames: once per game loop, a snapshot of the objects the game asks
 *      for (id, position, velocity). Positions are recorded instead of only
 *      inputs because our physics uses a variable deltaTime and the skull and
 *      platform move from the wall clock, so re-simulating inputs would not
 *      give the same result.
 *   2. Gameplay events: the replay system registers for Input, Collision,
 *      Death and Spawn like any other listener, and stores every one it sees
 *      while recording. During playback they are given back to the game at
 *      the same moment, so it can show what happened ("Death: enemy").
 *
 * Time: recording and playback both measure time in milliseconds from the
 * start of the recording, using the delta time the game passes to update().
 * main.cpp passes the delta of a real-time Timeline, so the replay shows what
 * was on screen. Playback speed scales that delta. Control events get their
 * timestamp from the game Timeline, like every other event.
 *
 * This file does not use SDL, so it can be tested without a window.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "EventManager.h"

// One object in one recorded frame. The id is chosen by the game
// (for example: client ID for players, 100 for the skull, 101 for the platform).
struct ReplayObjectState {
    int id = 0;
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
};

struct ReplayFrame {
    int64_t time = 0; // ms since the recording started
    std::vector<ReplayObjectState> objects;
};

struct ReplayEventRecord {
    int64_t time = 0; // ms since the recording started
    GameEvent event;
};

class ReplaySystem {
public:
    enum class State { Idle, Recording, Playing };

    // Game-provided functions (the replay system does not know what a player is)
    using SnapshotFn = std::function<std::vector<ReplayObjectState>()>;
    using ApplyFrameFn = std::function<void(const ReplayFrame&)>;
    using EventPlaybackFn = std::function<void(const GameEvent&)>;
    using StatusFn = std::function<void(const std::string&)>;
    using ClockFn = std::function<int64_t()>;

    // maxDurationMs: recording stops on its own after this long (bounds memory)
    ReplaySystem(EventManager& events, ClockFn clock, int64_t maxDurationMs = 60000)
        : events_(events), clock_(std::move(clock)), maxDurationMs_(maxDurationMs) {
        // Registration: control events
        sub(EventType::ReplayRecordStart, [this](const GameEvent&) { onRecordStart(); });
        sub(EventType::ReplayRecordStop, [this](const GameEvent&) { onRecordStop(); });
        sub(EventType::ReplayPlaybackStart, [this](const GameEvent& e) { onPlaybackStart(e.x); });
        sub(EventType::ReplayPlaybackStop, [this](const GameEvent&) { onPlaybackStop(); });
        sub(EventType::ReplayPlaybackSpeed, [this](const GameEvent& e) { setSpeed(e.x); });

        // Registration: gameplay events to record
        for (EventType t : {EventType::Input, EventType::Collision, EventType::Death, EventType::Spawn}) {
            sub(t, [this](const GameEvent& e) { onGameplayEvent(e); });
        }
    }

    ~ReplaySystem() {
        for (const auto& s : subscriptions_) events_.unsubscribe(s.first, s.second);
    }

    ReplaySystem(const ReplaySystem&) = delete;
    ReplaySystem& operator=(const ReplaySystem&) = delete;

    void setSnapshotSource(SnapshotFn fn) { snapshot_ = std::move(fn); }
    void setApplyFrame(ApplyFrameFn fn) { applyFrame_ = std::move(fn); }
    void setEventPlayback(EventPlaybackFn fn) { eventPlayback_ = std::move(fn); }
    void setStatusCallback(StatusFn fn) { status_ = std::move(fn); }

    // Helpers that RAISE the control events (they do not act directly).
    // The game binds these to keys; an in-game trigger can call them too.
    void requestRecordStart() { raiseControl(EventType::ReplayRecordStart); }
    void requestRecordStop() { raiseControl(EventType::ReplayRecordStop); }
    void requestPlaybackStart(float speed = 1.0f) { raiseControl(EventType::ReplayPlaybackStart, speed); }
    void requestPlaybackStop() { raiseControl(EventType::ReplayPlaybackStop); }
    void requestSpeed(float speed) { raiseControl(EventType::ReplayPlaybackSpeed, speed); }

    // Call once per game loop with the elapsed time in seconds.
    // Recording: stores one frame. Playing: moves the playback forward.
    void update(double deltaSeconds) {
        if (deltaSeconds < 0.0) deltaSeconds = 0.0;
        if (state_ == State::Recording) {
            if (stopRequested_) return; // waiting for our own RecordStop event
            elapsedMs_ += deltaSeconds * 1000.0;
            recordFrame();
            if (static_cast<int64_t>(elapsedMs_) >= maxDurationMs_) {
                stopRequested_ = true;
                notify("Replay: maximum length reached, recording stopped");
                requestRecordStop();
            }
        } else if (state_ == State::Playing) {
            if (stopRequested_) return; // waiting for our own PlaybackStop event
            cursorMs_ += deltaSeconds * 1000.0 * speed_;
            advancePlayback();
        }
    }

    State state() const { return state_; }
    bool isRecording() const { return state_ == State::Recording; }
    bool isPlaying() const { return state_ == State::Playing; }
    bool hasRecording() const { return !frames_.empty(); }
    float speed() const { return speed_; }
    std::size_t frameCount() const { return frames_.size(); }
    std::size_t eventCount() const { return recordedEvents_.size(); }
    int64_t durationMs() const { return frames_.empty() ? 0 : frames_.back().time; }
    int64_t playbackPositionMs() const { return static_cast<int64_t>(cursorMs_); }
    const std::vector<ReplayFrame>& frames() const { return frames_; }
    const std::vector<ReplayEventRecord>& recordedEvents() const { return recordedEvents_; }

private:
    void sub(EventType type, EventManager::Handler handler) {
        subscriptions_.push_back({type, events_.subscribe(type, std::move(handler))});
    }

    void raiseControl(EventType type, float x = 0.0f) {
        GameEvent e;
        e.type = type;
        e.priority = EventPriority::High;
        e.timestamp = clock_ ? clock_() : 0;
        e.detail = "replay";
        e.x = x;
        events_.raise(e); // local only: never sent over the network
    }

    void notify(const std::string& text) {
        if (status_) status_(text);
    }

    void recordFrame() {
        ReplayFrame frame;
        frame.time = static_cast<int64_t>(elapsedMs_);
        if (snapshot_) frame.objects = snapshot_();
        frames_.push_back(std::move(frame));
    }

    // Handling
    void onRecordStart() {
        if (state_ == State::Playing) {
            notify("Replay: stop playback before recording");
            return;
        }
        if (state_ == State::Recording) return; // already recording
        frames_.clear();
        recordedEvents_.clear();
        elapsedMs_ = 0.0;
        stopRequested_ = false;
        state_ = State::Recording;
        recordFrame(); // frame at time 0 = starting state
        notify("Replay: recording started");
    }

    void onRecordStop() {
        if (state_ != State::Recording) return;
        state_ = State::Idle;
        stopRequested_ = false;
        notify("Replay: recording stopped (" + std::to_string(frames_.size()) + " frames, " +
               std::to_string(recordedEvents_.size()) + " events)");
    }

    void onPlaybackStart(float speed) {
        if (state_ == State::Recording) onRecordStop(); // finish the recording first
        if (state_ == State::Playing) return;
        if (frames_.empty()) {
            notify("Replay: nothing recorded yet");
            return;
        }
        // Remember the live game so it can be put back after playback
        liveFrame_ = ReplayFrame{};
        if (snapshot_) liveFrame_.objects = snapshot_();
        setSpeed(speed > 0.0f ? speed : 1.0f);
        cursorMs_ = 0.0;
        frameIndex_ = 0;
        eventIndex_ = 0;
        stopRequested_ = false;
        state_ = State::Playing;
        if (applyFrame_) applyFrame_(frames_.front());
        notify("Replay: playback started");
        advancePlayback(); // gives back events recorded at time 0
    }

    void onPlaybackStop() {
        if (state_ != State::Playing) return;
        state_ = State::Idle;
        stopRequested_ = false;
        if (applyFrame_) applyFrame_(liveFrame_); // back to the live game
        notify("Replay: playback stopped");
    }

    void setSpeed(float speed) {
        if (speed > 0.0f) speed_ = std::clamp(speed, 0.25f, 4.0f);
    }

    void onGameplayEvent(const GameEvent& e) {
        if (state_ != State::Recording || stopRequested_) return;
        recordedEvents_.push_back({static_cast<int64_t>(elapsedMs_), e});
    }

    void advancePlayback() {
        const int64_t now = static_cast<int64_t>(cursorMs_);

        // Show the last frame whose time has been reached
        std::size_t newIndex = frameIndex_;
        while (newIndex + 1 < frames_.size() && frames_[newIndex + 1].time <= now) ++newIndex;
        if (newIndex != frameIndex_) {
            frameIndex_ = newIndex;
            if (applyFrame_) applyFrame_(frames_[frameIndex_]);
        }

        // Give back every recorded event whose time has been reached
        while (eventIndex_ < recordedEvents_.size() && recordedEvents_[eventIndex_].time <= now) {
            if (eventPlayback_) eventPlayback_(recordedEvents_[eventIndex_].event);
            ++eventIndex_;
        }

        // End of the recording: ask for playback to stop (handled on next dispatch)
        if (now >= frames_.back().time) {
            stopRequested_ = true;
            requestPlaybackStop();
        }
    }

    EventManager& events_;
    ClockFn clock_;
    int64_t maxDurationMs_;
    std::vector<std::pair<EventType, EventManager::SubscriptionId>> subscriptions_;

    SnapshotFn snapshot_;
    ApplyFrameFn applyFrame_;
    EventPlaybackFn eventPlayback_;
    StatusFn status_;

    State state_ = State::Idle;
    bool stopRequested_ = false;
    std::vector<ReplayFrame> frames_;
    std::vector<ReplayEventRecord> recordedEvents_;
    ReplayFrame liveFrame_;
    double elapsedMs_ = 0.0;
    double cursorMs_ = 0.0;
    float speed_ = 1.0f;
    std::size_t frameIndex_ = 0;
    std::size_t eventIndex_ = 0;
};

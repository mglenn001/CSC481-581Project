#pragma once

/*
 * Milestone 5 Section 2: Input Chords
 *
 * Detects CHORDS (several keys pressed together) and SEQUENCES (keys pressed
 * one after another, quickly) and raises an InputChord event for them through
 * the event manager. The game registers what it wants, for example:
 *
 *   ChordDetector chords(events, [](int key) { return Input::isKeyPressed((SDL_Scancode)key); });
 *   chords.addChord("shield", {SDL_SCANCODE_A, SDL_SCANCODE_D});         // A + D together
 *   chords.addSequence("dash-left", {SDL_SCANCODE_A, SDL_SCANCODE_A});   // double-tap A
 *   chords.update(timeline.getTime());   // once per frame
 *   events.subscribe(EventType::InputChord, [](const GameEvent& e) { if (e.detail == "shield") ... });
 *
 * Keys are plain ints (SDL scancodes in the games), and the key state comes
 * from a function the game passes in, so this file has no SDL code and can
 * be tested without a window.
 */

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "EventManager.h"

class ChordDetector {
public:
    using KeyStateFn = std::function<bool(int key)>;

    ChordDetector(EventManager& events, KeyStateFn keyDown)
        : events_(events), keyDown_(std::move(keyDown)) {}

    // Chord: all keys held at the same time, each pressed within windowMs of
    // the others. Fires once per press; release any key to fire it again.
    void addChord(const std::string& name, std::vector<int> keys, int64_t windowMs = 120) {
        chords_.push_back({name, std::move(keys), windowMs, false});
        watchAll();
    }

    // Sequence: keys pressed in this order, each within maxGapMs of the last.
    void addSequence(const std::string& name, std::vector<int> keys, int64_t maxGapMs = 250) {
        sequences_.push_back({name, std::move(keys), maxGapMs});
        watchAll();
    }

    // Call once per frame. nowMs = current time from the game's Timeline.
    // sourceId is copied into the raised events (for example the player's id).
    void update(int64_t nowMs, int sourceId = 0) {
        // 1. Find which watched keys went down this frame
        std::vector<int> pressedNow;
        for (int key : watched_) {
            bool down = keyDown_ && keyDown_(key);
            bool wasDown = held_.count(key) > 0;
            if (down && !wasDown) {
                held_.insert(key);
                lastPress_[key] = nowMs;
                pressedNow.push_back(key);
            } else if (!down && wasDown) {
                held_.erase(key);
            }
        }

        // 2. Chords: every key down, pressed close together, not fired yet
        for (Chord& c : chords_) {
            bool allDown = std::all_of(c.keys.begin(), c.keys.end(),
                                       [this](int k) { return held_.count(k) > 0; });
            if (!allDown) {
                c.fired = false;   // a key was let go: the chord can fire again
                continue;
            }
            if (c.fired) {
                continue;          // still held from last time: fire only once
            }
            int64_t first = nowMs, last = 0;
            for (int k : c.keys) {
                first = std::min(first, lastPress_[k]);
                last = std::max(last, lastPress_[k]);
            }
            if (last - first <= c.windowMs) {
                c.fired = true;
                raise(c.name, nowMs, sourceId);
            }
        }

        // 3. Sequences: remember recent presses, check whether one just finished
        for (int key : pressedNow) {
            history_.push_back({key, nowMs});
        }
        while (history_.size() > 16) {
            history_.pop_front();
        }
        if (!pressedNow.empty()) {
            for (const Sequence& s : sequences_) {
                if (matches(s)) {
                    raise(s.name, nowMs, sourceId);
                    history_.clear();   // a triple tap does not fire twice
                    break;
                }
            }
        }
    }

    // Forget all key state (for example after a pause or a respawn)
    void reset() {
        held_.clear();
        history_.clear();
        for (Chord& c : chords_) c.fired = false;
    }

private:
    struct Chord {
        std::string name;
        std::vector<int> keys;
        int64_t windowMs;
        bool fired;
    };
    struct Sequence {
        std::string name;
        std::vector<int> keys;
        int64_t maxGapMs;
    };

    void watchAll() {
        std::set<int> keys;
        for (const Chord& c : chords_) keys.insert(c.keys.begin(), c.keys.end());
        for (const Sequence& s : sequences_) keys.insert(s.keys.begin(), s.keys.end());
        watched_.assign(keys.begin(), keys.end());
    }

    // Do the last presses match this sequence, in order and quickly enough?
    bool matches(const Sequence& s) const {
        if (s.keys.empty() || history_.size() < s.keys.size()) {
            return false;
        }
        size_t start = history_.size() - s.keys.size();
        for (size_t i = 0; i < s.keys.size(); i++) {
            const auto& press = history_.at(start + i);
            if (press.first != s.keys.at(i)) {
                return false;
            }
            if (i > 0 && press.second - history_.at(start + i - 1).second > s.maxGapMs) {
                return false;
            }
        }
        return true;
    }

    void raise(const std::string& name, int64_t nowMs, int sourceId) {
        GameEvent e;
        e.type = EventType::InputChord;
        e.priority = EventPriority::High;
        e.timestamp = nowMs;
        e.sourceId = sourceId;
        e.targetId = sourceId;
        e.detail = name;
        events_.raise(e);
    }

    EventManager& events_;
    KeyStateFn keyDown_;
    std::vector<Chord> chords_;
    std::vector<Sequence> sequences_;
    std::vector<int> watched_;
    std::set<int> held_;
    std::map<int, int64_t> lastPress_;
    std::deque<std::pair<int, int64_t>> history_;   // (key, press time)
};

/*
 * Milestone 5 Section 2: headless test of the input chord detector.
 * A fake keyboard (a set of held keys) and a fake clock drive it frame by frame.
 */
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "../src/EventManager.h"
#include "../src/InputChords.h"

static int failures = 0;
#define CHECK(cond) do { if (cond) { std::cout << "  ok   " #cond "\n"; } \
    else { std::cout << "  FAIL " #cond "  (line " << __LINE__ << ")\n"; ++failures; } } while (0)

// Key numbers only matter to the test
enum { KEY_A = 4, KEY_D = 7, KEY_SPACE = 44 };

struct Rig {
    EventManager events;
    std::set<int> down;               // keys held on the fake keyboard
    int64_t now = 0;                  // fake clock (ms)
    std::vector<std::string> fired;   // chord events handled, in order
    ChordDetector chords{events, [this](int k) { return down.count(k) > 0; }};

    Rig() {
        chords.addChord("shield", {KEY_A, KEY_D}, 120);
        chords.addSequence("dash-left", {KEY_A, KEY_A}, 250);
        chords.addSequence("dash-right", {KEY_D, KEY_D}, 250);
        events.subscribe(EventType::InputChord, [this](const GameEvent& e) { fired.push_back(e.detail); });
    }
    // advance the clock by ms in 16 ms frames
    void wait(int ms) {
        for (int t = 0; t < ms; t += 16) { now += 16; chords.update(now, 1); events.dispatch(); }
    }
    void press(int k) { down.insert(k); wait(16); }
    void release(int k) { down.erase(k); wait(16); }
    void tap(int k) { press(k); wait(48); release(k); }
    int count(const std::string& name) const {
        int n = 0; for (auto& f : fired) if (f == name) n++; return n;
    }
};

int main() {
    std::cout << "1. A + D pressed together = shield, once per press\n";
    {
        Rig r;
        r.down.insert(KEY_A); r.down.insert(KEY_D); r.wait(16);
        r.wait(500);                          // keep holding
        CHECK(r.count("shield") == 1);
        r.release(KEY_D); r.press(KEY_D);     // re-press only D: A was pressed long ago
        CHECK(r.count("shield") == 1);
        r.down.clear(); r.wait(48);           // let go of both, press both again
        r.down.insert(KEY_A); r.down.insert(KEY_D); r.wait(16);
        CHECK(r.count("shield") == 2);
    }
    std::cout << "2. A held, D pressed much later = no shield (that is just changing direction)\n";
    {
        Rig r;
        r.press(KEY_A); r.wait(400); r.press(KEY_D); r.wait(200);
        CHECK(r.count("shield") == 0);
    }
    std::cout << "3. D pressed 80 ms after A = still a chord\n";
    {
        Rig r;
        r.press(KEY_A); r.wait(64); r.press(KEY_D);
        CHECK(r.count("shield") == 1);
    }
    std::cout << "4. Double-tap A = dash-left, double-tap D = dash-right\n";
    {
        Rig r;
        r.tap(KEY_A); r.wait(64); r.tap(KEY_A);
        CHECK(r.count("dash-left") == 1);
        r.wait(500);
        r.tap(KEY_D); r.wait(64); r.tap(KEY_D);
        CHECK(r.count("dash-right") == 1);
        CHECK(r.count("shield") == 0);
    }
    std::cout << "5. Two slow taps (more than 250 ms apart) = no dash\n";
    {
        Rig r;
        r.tap(KEY_A); r.wait(400); r.tap(KEY_A);
        CHECK(r.count("dash-left") == 0);
    }
    std::cout << "6. A then D quickly = no dash (wrong keys)\n";
    {
        Rig r;
        r.tap(KEY_A); r.wait(32); r.tap(KEY_D);
        CHECK(r.count("dash-left") == 0 && r.count("dash-right") == 0);
    }
    std::cout << "7. Triple tap = one dash, not two\n";
    {
        Rig r;
        r.tap(KEY_A); r.wait(32); r.tap(KEY_A); r.wait(32); r.tap(KEY_A);
        CHECK(r.count("dash-left") == 1);
    }
    std::cout << "8. Holding A (moving left) never dashes by itself\n";
    {
        Rig r;
        r.press(KEY_A); r.wait(2000);
        CHECK(r.fired.empty());
    }
    std::cout << "9. Chord events carry the clock time and the source id\n";
    {
        Rig r;
        int64_t stamp = -1; int src = -1;
        r.events.subscribe(EventType::InputChord, [&](const GameEvent& e) { stamp = e.timestamp; src = e.sourceId; });
        r.wait(100);
        r.down.insert(KEY_A); r.down.insert(KEY_D); r.wait(16);
        CHECK(stamp == r.now && src == 1);
    }
    std::cout << "10. reset() forgets key state\n";
    {
        Rig r;
        r.tap(KEY_A); r.chords.reset(); r.wait(32); r.tap(KEY_A);
        CHECK(r.count("dash-left") == 0);
    }

    std::cout << (failures == 0 ? "ALL CHORD TESTS PASSED\n" : "CHORD TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}

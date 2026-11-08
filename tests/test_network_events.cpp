/*
 * Headless test: two "clients" in one process exchange events through the
 * real networkingThread. No SDL needed.
 */
#include <cassert>
#include <iostream>
#include <thread>
#include "../src/Networking.h"
#include "../src/NetworkEvent.h"

int main() {
    // encode/decode round trip (detail containing spaces)
    GameEvent in{EventType::Death, EventPriority::Critical, 1500, 2, 2, "fell off map", 640.5f, 900.0f};
    std::string wire = NetEvent::encode(in);
    GameEvent out;
    assert(NetEvent::isEventMessage(wire.data(), wire.size()));
    assert(NetEvent::decode(wire.data(), wire.size(), out));
    assert(out.type == EventType::Death && out.priority == EventPriority::Critical);
    assert(out.sourceId == 2 && out.x == 640.5f && out.y == 900.0f && out.detail == "fell off map");
    assert(!NetEvent::decode("E 9 1 0 1 1 0 0 x", 17, out)); // bad type rejected
    std::cout << "encode/decode OK: " << wire << "\n";

    // two clients over real ZeroMQ sockets
    SharedData s1, s2;
    EventManager e1, e2;
    s1.eventManager = &e1; s2.eventManager = &e2;
    e1.setNetworkSink([&](const GameEvent& e){ s1.queueOutgoingEvent(e); });
    e2.setNetworkSink([&](const GameEvent& e){ s2.queueOutgoingEvent(e); });

    int c1Local = 0, c1Remote = 0, c2Local = 0, c2Remote = 0;
    e1.subscribe(EventType::Death, [&](const GameEvent& e){ (e.sourceId == 1 ? c1Local : c1Remote)++; });
    e2.subscribe(EventType::Death, [&](const GameEvent& e){ (e.sourceId == 2 ? c2Local : c2Remote)++; });

    std::thread t1(networkingThread, std::ref(s1), 1);
    std::thread t2(networkingThread, std::ref(s2), 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // let sockets connect

    // client 1 raises one networked death, client 2 raises one LOCAL-only death
    e1.raiseNetworked(GameEvent{EventType::Death, EventPriority::Critical, 0, 1, 1, "fall", 10, 20});
    e2.raise(GameEvent{EventType::Death, EventPriority::Critical, 0, 2, 2, "local only", 0, 0});
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    e1.dispatch(); e2.dispatch();
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // would catch any echo/loop
    e1.dispatch(); e2.dispatch();

    s1.running = false; s2.running = false;
    t1.join(); t2.join();

    std::cout << "c1 local=" << c1Local << " remote=" << c1Remote
              << " | c2 local=" << c2Local << " remote=" << c2Remote << "\n";
    assert(c1Local == 1); // sender handled its own event once
    assert(c2Remote == 1); // other client received it exactly once
    assert(c2Local == 1); // local-only event handled locally
    assert(c1Remote == 0); // local-only event was NOT sent; no echo back
    std::cout << "All tests passed.\n";
}
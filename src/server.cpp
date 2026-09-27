#include <zmq.hpp>

#include <iostream>
#include <string>
#include <unordered_map>
#include <sstream>
#include <thread>
#include <mutex>
#include <vector>
#include <cstring>
#include <chrono>

#define ZMQ_IO_THREADS 1
#define MAX_CLIENTS 3
#define BASE_PORT 5555

// Reserved "client" ID used to smuggle the platform's position
// through the same reply string as player positions. Real
// clients are always IDs 1..MAX_CLIENTS, so 0 is safe.
#define PLATFORM_ID 0

// The platform patrols horizontally across the ground gap
// (see GAP_START/GAP_END in main.cpp). These mirror that gap
// assuming the default 1920x1080 window.
//
// PLATFORM_Y is kept well above the enemy skull's patrol
// height (skull occupies roughly y=780-812) so jumping for
// the platform doesn't land the player in the skull's hitbox.
#define PLATFORM_MIN_X 890.0f
#define PLATFORM_MAX_X 1060.0f
#define PLATFORM_Y 630.0f
#define PLATFORM_SPEED 120.0f

struct PlayerState {
    float x;
    float y;
};

// Shared player information for all server threads
std::unordered_map<int, PlayerState> players;

// Protects the shared player map
std::mutex playersMutex;

// Server-authoritative platform position.
// Moved only by platformThread() below, driven by real wall-clock
// time. No client input can change it, so it is identical for
// every client regardless of that client's own Timeline speed.
struct PlatformState {
    float x;
    float y;
};

PlatformState platformState{PLATFORM_MIN_X, PLATFORM_Y};
std::mutex platformMutex;

// Moves the platform back and forth using real elapsed time,
// completely independent of any client's REQ/REP traffic.
void platformThread()
{
    int direction = 1;

    auto lastTick = std::chrono::steady_clock::now();

    while (true) {

        auto now = std::chrono::steady_clock::now();

        float realDeltaTime =
            std::chrono::duration<float>(now - lastTick).count();

        lastTick = now;

        {
            std::lock_guard<std::mutex> lock(platformMutex);

            platformState.x +=
                PLATFORM_SPEED * direction * realDeltaTime;

            if (platformState.x >= PLATFORM_MAX_X) {
                platformState.x = PLATFORM_MAX_X;
                direction = -1;
            }

            if (platformState.x <= PLATFORM_MIN_X) {
                platformState.x = PLATFORM_MIN_X;
                direction = 1;
            }
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(16)
        );
    }
}


/*
 * Each client gets its own server thread and its own REP socket.
 *
 * Client 1 -> port 5556
 * Client 2 -> port 5557
 * Client 3 -> port 5558
 *
 * This allows one client to communicate at a different rate
 * without blocking the other clients.
 */
void clientHandler(
    zmq::context_t& context,
    int assignedClientID
) {
    // Each thread owns its own ZeroMQ socket.
    // ZeroMQ sockets should not be shared between threads.
    zmq::socket_t responder(
        context,
        zmq::socket_type::rep
    );

    int port = BASE_PORT + assignedClientID;

    std::string address =
        "tcp://*:" + std::to_string(port);

    responder.bind(address);

    std::cout
        << "[Server Thread " << assignedClientID
        << "] Listening on port "
        << port
        << "..."
        << std::endl;


    bool hasReportedPosition = false;
    PlayerState lastReportedPosition{0.0f, 0.0f};


    while (true) {

        // Wait for this client's next update
        zmq::message_t request;

        auto result = responder.recv(
            request,
            zmq::recv_flags::none
        );

        if (!result) {
            continue;
        }


        std::string requestString(
            static_cast<char*>(request.data()),
            request.size()
        );


        /*
         * Expected message:
         *
         * clientID x y
         */
        std::istringstream input(requestString);

        int clientID;
        float x;
        float y;

        if (!(input >> clientID >> x >> y)) {

            // REP sockets must still send a reply
            std::string errorReply = "ERROR";

            responder.send(
                zmq::buffer(errorReply),
                zmq::send_flags::none
            );

            continue;
        }


        /*
         * Update the shared player state.
         *
         * Multiple client threads may access the player map
         * simultaneously, so this section is protected by
         * a mutex.
         */
        {
            std::lock_guard<std::mutex> lock(
                playersMutex
            );

            players[clientID] = {
                x,
                y
            };
        }


        // Keep terminal output readable.
        if (!hasReportedPosition) {

            std::cout
                << "Client " << clientID
                << " connected on server thread "
                << assignedClientID
                << " at position: ("
                << x << ", " << y << ")"
                << std::endl;

            lastReportedPosition = {
                x,
                y
            };

            hasReportedPosition = true;
        }
        else {

            float changeX =
                x - lastReportedPosition.x;

            float changeY =
                y - lastReportedPosition.y;


            if (
                changeX >= 50.0f ||
                changeX <= -50.0f ||
                changeY >= 50.0f ||
                changeY <= -50.0f
            ) {

                std::cout
                    << "Client " << clientID
                    << " moved to: ("
                    << x << ", " << y << ")"
                    << std::endl;

                lastReportedPosition = {
                    x,
                    y
                };
            }
        }


        /*
         * Create a snapshot of all player states.
         *
         * Format:
         *
         * ID1 x y;ID2 x y;ID3 x y;
         */
        std::ostringstream output;

        {
            std::lock_guard<std::mutex> lock(
                playersMutex
            );

            for (const auto& entry : players) {

                output
                    << entry.first << " "
                    << entry.second.x << " "
                    << entry.second.y << ";";
            }
        }


        /*
         * Append the server-authoritative platform position
         * using the reserved PLATFORM_ID. This is the same
         * position for every client's reply, because it only
         * ever comes from platformThread()'s real-time loop,
         * never from any client's own message rate/Timeline.
         */
        {
            std::lock_guard<std::mutex> lock(platformMutex);

            output
                << PLATFORM_ID << " "
                << platformState.x << " "
                << platformState.y << ";";
        }


        std::string replyString =
            output.str();


        // Reply only to this client.
        responder.send(
            zmq::buffer(replyString),
            zmq::send_flags::none
        );
    }
}


int main()
{
    /*
     * One ZeroMQ context can be shared between threads,
     * but each thread creates and owns its own socket.
     */
    zmq::context_t context(
        ZMQ_IO_THREADS
    );


    std::cout
        << "Asynchronous game server starting..."
        << std::endl;

    std::cout
        << "Creating dedicated server threads for "
        << MAX_CLIENTS
        << " clients."
        << std::endl;


    std::vector<std::thread> clientThreads;


    /*
     * Create one dedicated communication thread
     * for every supported client.
     */
    for (
        int clientID = 1;
        clientID <= MAX_CLIENTS;
        clientID++
    ) {

        clientThreads.emplace_back(
            clientHandler,
            std::ref(context),
            clientID
        );
    }


    /*
     * Start the platform's own thread. It never talks to any
     * client directly - it just updates shared state on real
     * time, which clientHandler() reads and forwards.
     */
    std::thread platformThreadHandle(platformThread);


    /*
     * Keep the server alive.
     *
     * Each client thread independently handles its
     * own synchronous REQ/REP communication.
     */
    for (auto& thread : clientThreads) {

        if (thread.joinable()) {
            thread.join();
        }
    }

    if (platformThreadHandle.joinable()) {
        platformThreadHandle.join();
    }


    return 0;
}
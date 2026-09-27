#include <zmq.hpp>

#include <iostream>
#include <string>
#include <unordered_map>
#include <sstream>
#include <thread>
#include <mutex>
#include <vector>
#include <cstring>

#define ZMQ_IO_THREADS 1
#define MAX_CLIENTS 3
#define BASE_PORT 5555

struct PlayerState {
    float x;
    float y;
};

// Shared player information for all server threads
std::unordered_map<int, PlayerState> players;

// Protects the shared player map
std::mutex playersMutex;


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


    return 0;
}
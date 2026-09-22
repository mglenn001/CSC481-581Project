#include <zmq.hpp>

#include <iostream>
#include <string>
#include <unordered_map>
#include <sstream>

#define THREADS 1

struct PlayerState {
    float x;
    float y;
};

int main()
{
    // Create ZeroMQ context
    zmq::context_t context(THREADS);

    // REP socket receives player updates from clients
    zmq::socket_t responder(context, zmq::socket_type::rep);

    responder.bind("tcp://*:5555");

    // Store the most recent position of every client
    std::unordered_map<int, PlayerState> players;

    // Stores the last position printed to the terminal
    std::unordered_map<int, PlayerState> lastReportedPositions;

    std::cout << "Game server started on port 5555..." << std::endl;
    std::cout << "Waiting for clients..." << std::endl;

    while (true) {

        // Receive update from a client
        zmq::message_t request;

        auto result = responder.recv(request, zmq::recv_flags::none);

        if (!result) {
            continue;
        }

        std::string requestString(static_cast<char*>(request.data()), request.size());

        /*
         * Expected message:
         * clientID x y
         */
        std::istringstream input(requestString);

        int clientID;
        float x;
        float y;

        if (input >> clientID >> x >> y) {

            // Check whether this client is new
            bool isNewClient =
                players.find(clientID) == players.end();

            // Always update the server's actual position.
            // This keeps networking smooth.
            players[clientID] = {x, y};

            if (isNewClient) {

                std::cout
                    << "Client " << clientID
                    << " connected at position: ("
                    << x << ", " << y << ")"
                    << std::endl;

                lastReportedPositions[clientID] = {x, y};
            }
            else {

                PlayerState last =
                    lastReportedPositions[clientID];

                float changeX = x - last.x;
                float changeY = y - last.y;

                // Only print after a noticeable movement
                if (changeX >= 50.0f || changeX <= -50.0f ||
                    changeY >= 50.0f || changeY <= -50.0f) {

                    std::cout
                        << "Client " << clientID
                        << " moved to: ("
                        << x << ", " << y << ")"
                        << std::endl;

                    lastReportedPositions[clientID] = {x, y};
                }
            }
        }

        /*
         * Send all known player positions back.
         * Format:
         * ID1 x y; ID2 x y;
         */
        std::ostringstream output;

        for (const auto& entry : players) {

            output
                << entry.first << " "
                << entry.second.x << " "
                << entry.second.y << ";";
        }

        std::string replyString = output.str();

        zmq::message_t reply(replyString.size());

        memcpy(
            reply.data(),
            replyString.data(),
            replyString.size()
        );

        responder.send(
            reply,
            zmq::send_flags::none
        );
    }

    return 0;
}
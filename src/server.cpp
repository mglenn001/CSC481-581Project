#include <zmq.hpp>

#include <iostream>
#include <string>
#include <unordered_map>
#include <sstream>

#define THREADS 1

// Store peer endpoint info
struct PeerInfo {
    int clientID;
    std::string ip;
    int port;
};

int main()
{
    // Create ZeroMQ context
    zmq::context_t context(THREADS);

    // REP socket receives player updates from clients
    zmq::socket_t responder(context, zmq::socket_type::rep);

    responder.bind("tcp://*:5555");

    // Track active peers registered with the server
    std::unordered_map<int, PeerInfo> activePeers;

    std::cout << "P2P Tracker Server started on port 5555..." << std::endl;

    while (true) {

        // Receive update from a client
        zmq::message_t request;

        auto result = responder.recv(request, zmq::recv_flags::none);

        if (!result) {
            continue;
        }

        std::string requestString(static_cast<char*>(request.data()), request.size());

        /*
         * Expected message format from client:
         * clientID peerPort x y
         */
        std::istringstream input(requestString);

        int clientID;
        int peerPort;
        float x;
        float y;

        if (input >> clientID >> peerPort >> x >> y) {
            // Register or update peer info
            activePeers[clientID] = { clientID, "127.0.0.1", peerPort };
        }

        // Send back list of active peer endpoints
        // Format: clientId ip port; clientId ip port;
        std::ostringstream output;
        for (const auto& entry : activePeers) {
            output << entry.second.clientID << " "
                   << entry.second.ip << " "
                   << entry.second.port << ";";
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
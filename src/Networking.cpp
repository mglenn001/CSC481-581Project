#include "Networking.h"

#include <zmq.hpp>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstring>

// Section 5: Ports used for direct peer-to-peer communication.
#define PEER_BASE_PORT 6000
#define TOTAL_PEERS 3

void networkingThread(
    SharedData& sharedData,
    int clientID
) {
    try {
        zmq::context_t context(1);

        // Section 5: This socket sends this player's position to the other clients.
        zmq::socket_t peerPublisher(context, zmq::socket_type::pub);
        peerPublisher.set(zmq::sockopt::linger,0);

        // Section 5: Each client has its own peer port. Client 1 -> 6001, Client 2 -> 6002, and Client 3 -> 6003
        int peerPort = PEER_BASE_PORT + clientID;
        std::string peerAddress = "tcp://*:" + std::to_string(peerPort);
        peerPublisher.bind(peerAddress);

        // This socket receives positions from the other clients.
        zmq::socket_t peerSubscriber(context, zmq::socket_type::sub);
        peerSubscriber.set(zmq::sockopt::linger,0);
        // Receive every peer message.
        peerSubscriber.set(zmq::sockopt::subscribe,"");

        // Section 5: Connect to the other clients.
        for (int peerID = 1; peerID <= 3; peerID++) {
            if (peerID == clientID) { continue; } // skip connecting to self
        
            int otherPeerPort = PEER_BASE_PORT + peerID;
            std::string otherPeerAddress = "tcp://localhost:" + std::to_string(otherPeerPort);
            peerSubscriber.connect(otherPeerAddress);
        }

        // Allow ZeroMQ time to establish P2P subscriptions across peers
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::cout << "Peer " << clientID << " P2P networking thread started on port " << peerPort << std::endl;

        while (sharedData.running.load()) {

            float localX;
            float localY;

            /*
             * Safely read the local player's
             * latest position.
             */
            {
                std::lock_guard<std::mutex> lock(sharedData.playerMutex);

                localX = sharedData.playerX;
                localY = sharedData.playerY;
            }

            // Section 5: Send this player's position directly to the other clients.
            // The server does not relay this data.
            std::ostringstream peerStream;
            peerStream << clientID << " " << localX << " " << localY;

            std::string peerString = peerStream.str();
            peerPublisher.send(zmq::buffer(peerString), zmq::send_flags::none);

            // Section 5: Non-blocking loop to receive messages from all other peers
            while (true) {
                zmq::message_t peerMessage;

                // Prevents this client from getting stuck waiting for another client.
                auto peerResult = peerSubscriber.recv(peerMessage, zmq::recv_flags::dontwait);

                // No more pending messages from peers
                if (!peerResult) { break; }

                std::string receivedString(static_cast<char*>(peerMessage.data()), peerMessage.size());
                std::stringstream entryStream(receivedString);

                int remoteID;
                float remoteX;
                float remoteY;

                if (entryStream >> remoteID >> remoteX >> remoteY) {
                    if (remoteID != clientID) {
                        // Thread-safe update of remote player position map
                        std::lock_guard<std::mutex> lock(sharedData.playerMutex);
                        sharedData.remotePlayers[remoteID] = {remoteX,remoteY};
                    }
                }
            }

            /*
             * Section 4: Asynchronicity
             *
             * The communication rate for this
             * client depends on its own Timeline
             * scale.
             *
             * 0.5x -> about 32 ms
             * 1.0x -> about 16 ms
             * 2.0x -> about 8 ms
             *
             * Each client has a dedicated server
             * communication thread, so changing
             * one client's rate does not slow
             * down or speed up the others.
             */
            double scale = sharedData.timeScale.load();

            int sleepMilliseconds = static_cast<int>(16.0 / scale);

            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMilliseconds));
        }

        // Clean resource shutdown
        peerPublisher.close();
        peerSubscriber.close();
        context.close();
    }
    catch (const zmq::error_t& e) {
        if (sharedData.running.load()) {
            std::cerr << "Networking error for client " << clientID << ": " << e.what() << std::endl;
        }
    }
}
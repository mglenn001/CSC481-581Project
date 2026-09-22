#include "Networking.h"

#include <zmq.hpp>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstring>

void networkingThread(
    SharedData& sharedData,
    int clientID
) {
    try {
        zmq::context_t context(1);
        zmq::socket_t requester(context, zmq::socket_type::req);

        // Prevent shutdown from hanging for a long time
        requester.set(zmq::sockopt::linger, 0);

        requester.connect("tcp://localhost:5555");

        std::cout
            << "Client " << clientID
            << " networking thread started."
            << std::endl;

        while (sharedData.running.load()) {

            float localX;
            float localY;

            // Safely read the local player's position
            {
                std::lock_guard<std::mutex> lock(
                    sharedData.playerMutex
                );

                localX = sharedData.playerX;
                localY = sharedData.playerY;
            }

            // Send local position
            std::ostringstream requestStream;

            requestStream
                << clientID << " "
                << localX << " "
                << localY;

            std::string requestString =
                requestStream.str();

            zmq::message_t request(
                requestString.size()
            );

            memcpy(
                request.data(),
                requestString.data(),
                requestString.size()
            );

            requester.send(
                request,
                zmq::send_flags::none
            );

            // Receive positions from server
            zmq::message_t reply;

            auto result = requester.recv(
                reply,
                zmq::recv_flags::none
            );

            if (!result) {
                continue;
            }

            std::string replyString(
                static_cast<char*>(reply.data()),
                reply.size()
            );

            std::unordered_map<int, RemotePlayerState>
                updatedRemotePlayers;

            std::stringstream playerStream(replyString);
            std::string playerEntry;

            while (
                std::getline(
                    playerStream,
                    playerEntry,
                    ';'
                )
            ) {
                if (playerEntry.empty()) {
                    continue;
                }

                std::stringstream entryStream(
                    playerEntry
                );

                int remoteID;
                float remoteX;
                float remoteY;

                if (
                    entryStream
                    >> remoteID
                    >> remoteX
                    >> remoteY
                ) {
                    if (remoteID != clientID) {
                        updatedRemotePlayers[remoteID] = {
                            remoteX,
                            remoteY
                        };
                    }
                }
            }

            // Safely update shared remote player data
            {
                std::lock_guard<std::mutex> lock(
                    sharedData.playerMutex
                );

                sharedData.remotePlayers =
                    updatedRemotePlayers;
            }

            // Don't flood the server unnecessarily
            std::this_thread::sleep_for(
                std::chrono::milliseconds(16)
            );
        }

        requester.close();
        context.close();
    }
    catch (const zmq::error_t& e) {
        if (sharedData.running.load()) {
            std::cerr
                << "Networking error: "
                << e.what()
                << std::endl;
        }
    }
}
#include "Networking.h"

#include <zmq.hpp>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstring>

#define BASE_PORT 5555

void networkingThread(
    SharedData& sharedData,
    int clientID
) {
    try {
        zmq::context_t context(1);

        zmq::socket_t requester(
            context,
            zmq::socket_type::req
        );

        // Prevent shutdown from hanging
        requester.set(
            zmq::sockopt::linger,
            0
        );

        /*
         * Each client connects to its own dedicated
         * server thread/port.
         *
         * Client 1 -> 5556
         * Client 2 -> 5557
         * Client 3 -> 5558
         */
        int port =
            BASE_PORT + clientID;

        std::string address =
            "tcp://localhost:"
            + std::to_string(port);

        requester.connect(address);

        std::cout
            << "Client " << clientID
            << " networking thread started."
            << std::endl;

        std::cout
            << "Client " << clientID
            << " connected to server port "
            << port
            << "."
            << std::endl;


        while (sharedData.running.load()) {

            float localX;
            float localY;

            /*
             * Safely read the local player's
             * latest position.
             */
            {
                std::lock_guard<std::mutex> lock(
                    sharedData.playerMutex
                );

                localX =
                    sharedData.playerX;

                localY =
                    sharedData.playerY;
            }


            /*
             * Send this client's current position.
             *
             * Format:
             *
             * clientID x y
             */
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


            /*
             * Wait for this client's dedicated
             * server thread to reply.
             */
            zmq::message_t reply;

            auto result =
                requester.recv(
                    reply,
                    zmq::recv_flags::none
                );

            if (!result) {
                continue;
            }


            std::string replyString(
                static_cast<char*>(
                    reply.data()
                ),
                reply.size()
            );


            /*
             * Parse all player positions
             * returned by the server.
             */
            std::unordered_map<
                int,
                RemotePlayerState
            > updatedRemotePlayers;


            std::stringstream playerStream(
                replyString
            );

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

                    /*
                     * Don't store this client's
                     * own position as a remote
                     * player.
                     */
                    if (remoteID != clientID) {

                        updatedRemotePlayers[
                            remoteID
                        ] = {
                            remoteX,
                            remoteY
                        };
                    }
                }
            }


            /*
             * Safely update the remote player
             * data used by the main/render thread.
             */
            {
                std::lock_guard<std::mutex> lock(
                    sharedData.playerMutex
                );

                sharedData.remotePlayers =
                    updatedRemotePlayers;
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
            double scale =
                sharedData.timeScale.load();

            int sleepMilliseconds =
                static_cast<int>(
                    16.0 / scale
                );

            std::this_thread::sleep_for(
                std::chrono::milliseconds(
                    sleepMilliseconds
                )
            );
        }


        requester.close();
        context.close();
    }
    catch (const zmq::error_t& e) {

        if (sharedData.running.load()) {

            std::cerr
                << "Networking error for client "
                << clientID
                << ": "
                << e.what()
                << std::endl;
        }
    }
}
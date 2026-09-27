#include "Networking.h"

#include <zmq.hpp>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstring>

#define BASE_PORT 5555
// Section 5: Ports used for direct peer-to-peer communication.
#define PEER_BASE_PORT 6000

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
            if (peerID == clientID) { continue; }
        
            int otherPeerPort = PEER_BASE_PORT + peerID;
            std::string otherPeerAddress = "tcp://localhost:" + std::to_string(otherPeerPort);
            peerSubscriber.connect(otherPeerAddress);
        }

        // Allow ZeroMQ time to establish P2P subscriptions across peers
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

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

        std::cout << "Client " << clientID << " peer port " << peerPort << "." << std::endl;

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
             * Ask the server for the current
             * platform position.
             *
             * The server no longer needs to
             * relay player positions.
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

            // Section 5: Send this player's position directly to the other clients.
            // The server does not relay this data.
            std::ostringstream peerStream;
            peerStream << clientID << " " << localX << " " << localY;

            std::string peerString = peerStream.str();
            peerPublisher.send(zmq::buffer(peerString), zmq::send_flags::none);


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
             * The server reply contains the
             * server-authoritative platform.
             * 
             * ID 0 = platform.
             */
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

                    // ID 0 is the server's moving platform.
                    if (remoteID == 0) {
                        std::lock_guard<std::mutex> lock(sharedData.playerMutex);
                        sharedData.platformX = remoteX;
                        sharedData.platformY = remoteY;
                    }
                }
            }

            // Section 5: Check for player positions sent directly from the other clients.
            while (true) {
                zmq::message_t peerMessage;

                // Prevents this client from getting stuck waiting for another client.
                auto peerResult = peerSubscriber.recv(peerMessage, zmq::recv_flags::dontwait);

                if (!peerResult) { break; }

                std::string peerString(static_cast<char*>(peerMessage.data()), peerMessage.size());

                std::stringstream peerStream(peerString);

                int remoteID;
                float remoteX;
                float remoteY;

                if (peerStream >> remoteID >> remoteX >> remoteY) {
                    // Do not store this client's own position as a remote player.
                    if (remoteID != clientID) {
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
        peerPublisher.close();
        peerSubscriber.close();
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
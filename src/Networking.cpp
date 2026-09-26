#include "Networking.h"

#include <zmq.hpp>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstring>

// Thread that listens for direct position updates from other peers
void peerListenerThread(SharedData& sharedData, int p2pPort) {
    try {
        zmq::context_t context(1);
        zmq::socket_t receiver(context, zmq::socket_type::pull);

        // Prevent socket from blocking shutdown indefinitely
        receiver.set(zmq::sockopt::linger, 0);
        // Set a 200ms receive timeout so loop checks sharedData.running regularly
        receiver.set(zmq::sockopt::rcvtimeo, 200);
        
        // Bind to local peer-to-peer port
        std::string bindAddr = "tcp://*:" + std::to_string(p2pPort);
        receiver.bind(bindAddr);

        while (sharedData.running.load()) {
            zmq::message_t msg;
            auto res = receiver.recv(msg, zmq::recv_flags::none);
            if (!res) continue;

            std::string msgStr(static_cast<char*>(msg.data()), msg.size());
            std::stringstream ss(msgStr);

            int remoteID;
            float remoteX, remoteY;

            // Message format: clientID x y
            if (ss >> remoteID >> remoteX >> remoteY) {
                std::lock_guard<std::mutex> lock(sharedData.playerMutex);
                sharedData.remotePlayers[remoteID] = { remoteX, remoteY };
            }
        }

        receiver.close();
        context.close();
    }
    catch (const zmq::error_t& e) {
        if (sharedData.running.load()) {
            std::cerr << "Peer listener error: " << e.what() << std::endl;
        }
    }
}

// Main networking thread: queries server for peers & sends P2P updates directly to peers
void networkingThread(
    SharedData& sharedData,
    int clientID
) {
    int p2pPort = 6000 + clientID; // Assign a unique P2P port per client ID

    // Start background thread to receive peer updates
    std::thread listener(peerListenerThread, std::ref(sharedData), p2pPort);

    try {
        zmq::context_t context(1);
        zmq::socket_t requester(context, zmq::socket_type::req);

        // Prevent shutdown from hanging for a long time
        requester.set(zmq::sockopt::linger, 0);
        requester.set(zmq::sockopt::rcvtimeo, 200); // 200ms receive timeout
        requester.set(zmq::sockopt::sndtimeo, 200); // 200ms send timeout

        requester.connect("tcp://localhost:5555");

        // Map active peer IDs to dedicated PUSH sockets for sending direct updates
        std::unordered_map<int, std::unique_ptr<zmq::socket_t>> peerSockets;

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

            // Tell server our clientID and P2P listening port to get active peer list
            std::ostringstream requestStream;

            requestStream
                << clientID << " "
                << p2pPort << " "
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

            auto sendRes = requester.send(request, zmq::send_flags::none);

            if (sendRes) {
                // Receive list of peers from server
                zmq::message_t reply;
                auto recvRes = requester.recv(reply, zmq::recv_flags::none);

                if (recvRes) {
                    std::string replyStr(static_cast<char*>(reply.data()), reply.size());
                    std::stringstream ss(replyStr);
                    std::string peerEntry;

                    // Parse discovered peers: clientID ip port
                    while (std::getline(ss, peerEntry, ';')) {
                        if (peerEntry.empty()) continue;
                        std::stringstream entrySS(peerEntry);

                        int peerID, port;
                        std::string ip;

                        if (entrySS >> peerID >> ip >> port) {
                            // Connect to peer if new
                            if (peerID != clientID && peerSockets.find(peerID) == peerSockets.end()) {
                                auto pushSock = std::make_unique<zmq::socket_t>(context, zmq::socket_type::push);
                                pushSock->set(zmq::sockopt::linger, 0);
                                pushSock->set(zmq::sockopt::sndtimeo, 100);
                                pushSock->connect("tcp://" + ip + ":" + std::to_string(port));
                                peerSockets[peerID] = std::move(pushSock);
                            }
                        }
                    }
                }
            }

            // Send player coordinates directly to each connected peer
            if (sharedData.running.load()) {
                std::ostringstream peerMsgStream;
                peerMsgStream << clientID << " " << localX << " " << localY;
                std::string peerMsgStr = peerMsgStream.str();

                for (auto& [id, socket] : peerSockets) {
                    zmq::message_t pMsg(peerMsgStr.size());
                    memcpy(pMsg.data(), peerMsgStr.data(), peerMsgStr.size());
                    socket->send(pMsg, zmq::send_flags::dontwait);
                }
            }

            // Don't flood the server unnecessarily
            std::this_thread::sleep_for(
                std::chrono::milliseconds(16)
            );
        }

        // Clean up sockets explicitly
        for (auto& [id, socket] : peerSockets) {
            if (socket) socket->close();
        }
        peerSockets.clear();

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
    if (listener.joinable()) {
        listener.join();
    }
}
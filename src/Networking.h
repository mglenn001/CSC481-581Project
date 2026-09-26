#pragma once

#include "SharedData.h"

// Listens on a PULL socket for incoming peer updates
void peerListenerThread(SharedData& sharedData, int p2pPort);

// Main networking thread that tracks server discovery and sends local updates directly to peers
void networkingThread(
    SharedData& sharedData,
    int clientID
);
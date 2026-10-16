#pragma once

// Milestone 3 Section C.1: Second data format for the performance comparison.
//
// The engine can send a player's position to its peers in one of two ways.
// Only the data REPRESENTATION changes. The gameplay and the P2P PUB/SUB
// networking are identical for both formats.
//
//   TEXT   (original) : a string such as "2 640.5 300"
//                       Built with ostringstream, parsed with stringstream.
//   BINARY (new)      : a fixed-size raw struct (10 bytes), copied with memcpy.
//                       No string formatting or parsing is needed.
//
// Choose a format when starting a client:
//   ./main 1            -> TEXT   (default, same behavior as before)
//   ./main 1 text       -> TEXT
//   ./main 1 binary     -> BINARY
//
// Every receiver detects the format of each incoming message on its own
// (see decode below), so clients using different formats can still talk to
// each other. For fair experiments, run all clients with the same format.

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

enum class NetworkFormat {
    TEXT,
    BINARY
};

namespace NetFormat {

// First byte of every binary packet. It is not a printable ASCII character,
// and text packets always start with a digit (the client ID), so the two
// formats can never be confused with each other.
constexpr uint8_t BINARY_MAGIC = 0xB1;

// Raw layout of one binary packet.
// pack(1) removes padding so the struct is exactly 10 bytes on the wire.
// (Assumes all clients run on the same kind of machine, so byte order matches.)
#pragma pack(push, 1)
struct BinaryPlayerPacket {
    uint8_t magic;     // always BINARY_MAGIC
    uint8_t clientID;  // 1..4
    float   x;
    float   y;
};
#pragma pack(pop)

static_assert(sizeof(BinaryPlayerPacket) == 10, "BinaryPlayerPacket must be 10 bytes");

// Parse a format name from the command line. Unknown names fall back to TEXT.
inline NetworkFormat parseFormat(const std::string& name) {
    return (name == "binary") ? NetworkFormat::BINARY : NetworkFormat::TEXT;
}

// Human-readable name, used for log messages.
inline const char* formatName(NetworkFormat format) {
    return (format == NetworkFormat::BINARY) ? "BINARY" : "TEXT";
}

// Turn (clientID, x, y) into the bytes that are sent over the network.
inline std::string encode(NetworkFormat format, int clientID, float x, float y) {
    if (format == NetworkFormat::BINARY) {
        BinaryPlayerPacket packet;
        packet.magic = BINARY_MAGIC;
        packet.clientID = static_cast<uint8_t>(clientID);
        packet.x = x;
        packet.y = y;
        // std::string can hold arbitrary bytes, so it works as a byte buffer.
        return std::string(reinterpret_cast<const char*>(&packet), sizeof(packet));
    }

    // TEXT: exactly the same string the original code produced.
    std::ostringstream stream;
    stream << clientID << " " << x << " " << y;
    return stream.str();
}

// Turn received bytes back into (clientID, x, y).
// Detects the format from the data itself. Returns false if it is malformed.
inline bool decode(const void* data, size_t size, int& clientID, float& x, float& y) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);

    // BINARY: right size and starts with the magic byte.
    if (size == sizeof(BinaryPlayerPacket) && bytes[0] == BINARY_MAGIC) {
        BinaryPlayerPacket packet;
        std::memcpy(&packet, bytes, sizeof(packet));
        clientID = packet.clientID;
        x = packet.x;
        y = packet.y;
        return true;
    }

    // TEXT: parse "id x y" (same parsing the original code used).
    std::stringstream stream(std::string(static_cast<const char*>(data), size));
    return static_cast<bool>(stream >> clientID >> x >> y);
}

} // namespace NetFormat
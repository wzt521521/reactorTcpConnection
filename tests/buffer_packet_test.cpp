#include <arpa/inet.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "Buffer.hpp"
#include "Packet.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::string encodeFrame(uint16_t command, const std::string& body) {
    Buffer buffer;
    Packet::encode(&buffer, command, body);
    return std::string(buffer.peekRead(), buffer.getReadable());
}

}  // namespace

int main() {
    const std::string body = "packet-body";
    const std::string frame = encodeFrame(0x1234, body);

    require(frame.size() == Packet::KHeaderLen + body.size(),
            "encoded frame length mismatch");

    uint32_t totalNetwork = 0;
    uint16_t commandNetwork = 0;
    std::memcpy(&totalNetwork, frame.data(), sizeof(totalNetwork));
    std::memcpy(&commandNetwork,
                frame.data() + sizeof(totalNetwork),
                sizeof(commandNetwork));

    require(ntohl(totalNetwork) == frame.size(),
            "total length is not encoded in network byte order");
    require(ntohs(commandNetwork) == 0x1234,
            "command is not encoded in network byte order");
    require(frame.substr(Packet::KHeaderLen) == body,
            "encoded body mismatch");

    Packet codec;
    int callbackCount = 0;
    uint16_t decodedCommand = 0;
    std::string decodedBody;
    codec.setFrameCallback(
        [&](const std::shared_ptr<Tcpconnection>&,
            uint16_t command,
            const std::string& decoded) {
            ++callbackCount;
            decodedCommand = command;
            decodedBody = decoded;
        });

    Buffer fragmented;
    fragmented.append(3, frame.data());
    require(codec.decode(nullptr, &fragmented),
            "three-byte partial header should be accepted");
    require(callbackCount == 0,
            "partial frame must not invoke callback");

    fragmented.append(frame.size() - 3, frame.data() + 3);
    require(codec.decode(nullptr, &fragmented),
            "complete fragmented frame should decode");
    require(callbackCount == 1,
            "complete frame should invoke callback once");
    require(decodedCommand == 0x1234 && decodedBody == body,
            "decoded frame content mismatch");

    const std::string second = encodeFrame(7, "second");
    Buffer sticky;
    sticky.append(frame.size(), frame.data());
    sticky.append(second.size(), second.data());
    callbackCount = 0;
    require(codec.decode(nullptr, &sticky),
            "two sticky frames should decode");
    require(callbackCount == 2,
            "two sticky frames should invoke two callbacks");
    require(sticky.getReadable() == 0,
            "decoded frames should be consumed");

    Buffer invalid;
    uint32_t invalidTotal = htonl(5);
    invalid.append(sizeof(invalidTotal),
                   reinterpret_cast<const char*>(&invalidTotal));
    require(!codec.decode(nullptr, &invalid),
            "frame shorter than header must be rejected");

    const std::string large(70 * 1024, 'x');
    Buffer largeBuffer;
    largeBuffer.append(large.size(), large.data());
    require(largeBuffer.getReadable() == large.size(),
            "large append should grow Buffer");
    require(std::string(largeBuffer.peekRead(), largeBuffer.getReadable()) == large,
            "large append content mismatch");

    std::cout << "buffer_packet_test passed\n";
    return EXIT_SUCCESS;
}

//
// Example subscriber process: listens on a TCP port, subscribes to a topic, and
// prints every message it receives until it sees the sentinel payload "bye".
//
// Run (after building):   ./cmake-build-debug/examples/subscriber [port]
//

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <thread>

#include "common/process_communications/TCPTransport.hpp"

namespace {

    auto as_text(std::span<const std::byte> bytes) -> std::string {
        std::string out(bytes.size(), '\0');
        std::memcpy(out.data(), bytes.data(), bytes.size());
        return out;
    }

} // namespace

int main(int argc, char **argv) {
    using common::process_communications::tcp_transport::TcpTransport;

    const std::uint16_t port =
            (argc > 1) ? static_cast<std::uint16_t>(std::stoi(argv[1])) : 9100U;

    TcpTransport server{TcpTransport::TcpTransportConfig{.m_port = port}};
    if (const auto listening = server.start_listening(); !listening) {
        std::cerr << "[subscriber] failed to start listening on port " << port << '\n';
        return 1;
    }
    std::cout << "[subscriber] listening on port " << port << '\n';

    bool done{false};
    server.subscribe("telemetry", [&done](std::span<const std::byte> bytes) {
        const auto message = as_text(bytes);
        std::cout << "[subscriber] received: " << message << '\n';
        if (message == "bye") {
            done = true;
        }
    });

    // Non-blocking event loop: poll() accepts the connection, drains the socket,
    // and dispatches whole frames to the handler above.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        (void) server.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "[subscriber] exiting\n";
    return 0;
}

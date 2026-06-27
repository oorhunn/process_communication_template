//
// Example publisher process: connects to a subscriber on localhost and sends a
// handful of "telemetry" messages, finishing with the sentinel payload "bye".
//
// Run (after the subscriber is up):  ./cmake-build-debug/examples/publisher [port]
//

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "process_communications/TCPTransport.hpp"

namespace {

    auto as_bytes(std::string_view text) -> std::vector<std::byte> {
        std::vector<std::byte> out(text.size());
        std::memcpy(out.data(), text.data(), text.size());
        return out;
    }

    // Pump the transport for a short while so non-blocking sends actually flush
    // (publish() queues the frame; poll() drives the bytes out of the socket).
    void pump(lib::process_communications::tcp_transport::TcpTransport &transport,
              std::chrono::milliseconds duration) {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < deadline) {
            (void) transport.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

} // namespace

int main(int argc, char **argv) {
    using lib::process_communications::tcp_transport::TcpTransport;

    const std::uint16_t port =
            (argc > 1) ? static_cast<std::uint16_t>(std::stoi(argv[1])) : 9100U;

    TcpTransport client{TcpTransport::TcpTransportConfig{.m_port = port, .m_host = "127.0.0.1"}};
    if (const auto connected = client.connect_to_peer(); !connected) {
        std::cerr << "[publisher] failed to connect on port " << port << '\n';
        return 1;
    }
    std::cout << "[publisher] connecting to 127.0.0.1:" << port << '\n';

    // Give the non-blocking connect a moment to complete before sending.
    pump(client, std::chrono::milliseconds(200));

    for (int i = 1; i <= 5; ++i) {
        const auto message = "telemetry message #" + std::to_string(i);
        if (const auto result = client.publish("telemetry", as_bytes(message)); !result) {
            std::cerr << "[publisher] publish failed\n";
            return 1;
        }
        std::cout << "[publisher] sent: " << message << '\n';
        pump(client, std::chrono::milliseconds(100));
    }

    // Sentinel so the subscriber knows to shut down cleanly.
    (void) client.publish("telemetry", as_bytes("bye"));
    std::cout << "[publisher] sent: bye\n";
    pump(client, std::chrono::milliseconds(200));

    std::cout << "[publisher] done\n";
    return 0;
}

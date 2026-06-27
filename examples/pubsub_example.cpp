//
// Example: publish / subscribe over the two transports.
//
// Build target: pubsub_example (see examples/CMakeLists.txt).
// Run:          ./cmake-build-debug/examples/pubsub_example
//

#include <cstddef>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "process_communications/InprocTransport.hpp"
#include "process_communications/TCPTransport.hpp"

namespace {

    // The transports speak std::span<const std::byte>; these two helpers just
    // bridge to/from plain text so the example stays readable.
    auto as_bytes(std::string_view text) -> std::vector<std::byte> {
        std::vector<std::byte> out(text.size());
        std::memcpy(out.data(), text.data(), text.size());
        return out;
    }

    auto as_text(std::span<const std::byte> bytes) -> std::string {
        std::string out(bytes.size(), '\0');
        std::memcpy(out.data(), bytes.data(), bytes.size());
        return out;
    }

    // ---- In-process pub/sub (same address space, synchronous) --------------
    void inproc_example() {
        using lib::process_communications::inproc_transport::InprocTransport;
        using lib::process_communications::inproc_transport::InprocTransportConfig;

        InprocTransport bus{InprocTransportConfig{}};

        // 1) Subscribe: register a handler for a topic key.
        bus.subscribe("orders", [](std::span<const std::byte> bytes) {
            std::cout << "[inproc] orders handler received: " << as_text(bytes) << '\n';
        });

        // 2) Publish: handlers for the key are invoked synchronously, inline.
        const auto payload = as_bytes("order#42");
        if (const auto result = bus.publish("orders", payload); !result) {
            std::cerr << "[inproc] publish failed\n";
        }
    }

    // ---- TCP pub/sub (cross-process; here both ends are in one program) -----
    void tcp_example() {
        using lib::process_communications::tcp_transport::TcpTransport;

        constexpr std::uint16_t port{9100U};

        // Server: bind + listen, and subscribe to the topic it cares about.
        TcpTransport server{TcpTransport::TcpTransportConfig{.m_port = port}};
        if (const auto listening = server.start_listening(); !listening) {
            std::cerr << "[tcp] server failed to start listening\n";
            return;
        }
        server.subscribe("telemetry", [](std::span<const std::byte> bytes) {
            std::cout << "[tcp] telemetry handler received: " << as_text(bytes) << '\n';
        });

        // Client: connect to the server's port on localhost.
        TcpTransport client{TcpTransport::TcpTransportConfig{.m_port = port}};
        if (const auto connected = client.connect_to_peer(); !connected) {
            std::cerr << "[tcp] client failed to connect\n";
            return;
        }

        // Both sockets are non-blocking, so the accept + handshake complete over
        // a few poll() ticks. poll() accepts, drains, dispatches, and flushes.
        for (int tick = 0; tick < 100 && !server.has_peer(); ++tick) {
            (void) server.poll();
            (void) client.poll();
        }

        // Publish from the client; it gets framed and queued, then sent on flush.
        if (const auto result = client.publish("telemetry", as_bytes("cpu=42%")); !result) {
            std::cerr << "[tcp] publish failed\n";
            return;
        }

        // Pump until the server dispatches the frame to its subscriber.
        for (int tick = 0; tick < 100; ++tick) {
            (void) client.poll();
            if (server.poll() > 0U) {
                break;
            }
        }
    }

} // namespace

int main() {
    inproc_example();
    tcp_example();
    return 0;
}

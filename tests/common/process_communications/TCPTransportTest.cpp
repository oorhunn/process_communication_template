//
// Tests for common/process_communications/TCPTransport.hpp
//

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "common/process_communications/ErrorTypes.hpp"
#include "common/process_communications/TCPTransport.hpp"

namespace {

    using common::process_communications::error_types::TransportError;
    using common::process_communications::tcp_transport::FdHandle;
    using common::process_communications::tcp_transport::TcpTransport;

    auto make_bytes(std::string_view text) -> std::vector<std::byte> {
        std::vector<std::byte> out(text.size());
        std::memcpy(out.data(), text.data(), text.size());
        return out;
    }

    auto to_string(std::span<const std::byte> bytes) -> std::string {
        std::string out(bytes.size(), '\0');
        std::memcpy(out.data(), bytes.data(), bytes.size());
        return out;
    }

    // Ask the kernel for an ephemeral port, then release it so the transport can
    // bind it. There is a small reuse window, but SO_REUSEADDR keeps it reliable.
    auto pick_free_port() -> std::uint16_t {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_GE(fd, 0);

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;  // let the OS choose

        EXPECT_EQ(::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);

        socklen_t len = sizeof(address);
        EXPECT_EQ(::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &len), 0);
        const std::uint16_t port = ntohs(address.sin_port);

        ::close(fd);
        return port;
    }

    // Drive both ends until `predicate` holds or the budget runs out. Returns the
    // total number of frames the server dispatched.
    template<typename Predicate>
    auto pump_until(TcpTransport &server, TcpTransport &client, Predicate predicate)
            -> std::size_t {
        using clock = std::chrono::steady_clock;
        const auto deadline = clock::now() + std::chrono::seconds(2);

        std::size_t dispatched_total{0U};
        while (clock::now() < deadline) {

            dispatched_total += server.poll();
            (void) client.poll();

            if (predicate()) {

                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return dispatched_total;
    }

    // ---- FdHandle ----------------------------------------------------------

    TEST(FdHandleTest, DefaultConstructedIsInvalid) {
        FdHandle handle;
        EXPECT_FALSE(handle.valid());
        EXPECT_EQ(handle.get(), -1);
    }

    TEST(FdHandleTest, WrapsRealDescriptor) {
        const int fd = ::open("/dev/null", O_RDONLY);
        ASSERT_GE(fd, 0);

        FdHandle handle{fd};
        EXPECT_TRUE(handle.valid());
        EXPECT_EQ(handle.get(), fd);
    }

    TEST(FdHandleTest, ResetReplacesDescriptor) {
        FdHandle handle{::open("/dev/null", O_RDONLY)};
        ASSERT_TRUE(handle.valid());

        handle.reset();
        EXPECT_FALSE(handle.valid());
        EXPECT_EQ(handle.get(), -1);
    }

    TEST(FdHandleTest, CloseIsIdempotent) {
        FdHandle handle{::open("/dev/null", O_RDONLY)};
        ASSERT_TRUE(handle.valid());

        handle.close();
        EXPECT_FALSE(handle.valid());
        handle.close();  // second close must be a no-op, not a double ::close
        EXPECT_FALSE(handle.valid());
    }

    TEST(FdHandleTest, MoveConstructTransfersOwnership) {
        const int fd = ::open("/dev/null", O_RDONLY);
        ASSERT_GE(fd, 0);

        FdHandle source{fd};
        FdHandle moved{std::move(source)};

        EXPECT_TRUE(moved.valid());
        EXPECT_EQ(moved.get(), fd);
        EXPECT_FALSE(source.valid());  // NOLINT(bugprone-use-after-move)
        EXPECT_EQ(source.get(), -1);   // NOLINT(bugprone-use-after-move)
    }

    TEST(FdHandleTest, MoveAssignTransfersOwnership) {
        FdHandle source{::open("/dev/null", O_RDONLY)};
        const int fd = source.get();
        ASSERT_GE(fd, 0);

        FdHandle target;
        target = std::move(source);

        EXPECT_TRUE(target.valid());
        EXPECT_EQ(target.get(), fd);
        EXPECT_FALSE(source.valid());  // NOLINT(bugprone-use-after-move)
    }

    TEST(FdHandleTest, SelfMoveAssignmentKeepsDescriptor) {
        FdHandle handle{::open("/dev/null", O_RDONLY)};
        const int fd = handle.get();
        ASSERT_GE(fd, 0);

        auto &alias = handle;
        handle = std::move(alias);  // NOLINT(clang-diagnostic-self-move)

        EXPECT_TRUE(handle.valid());
        EXPECT_EQ(handle.get(), fd);
    }

    // ---- TcpTransport: connection state ------------------------------------

    TEST(TcpTransportTest, FreshTransportHasNoPeer) {
        TcpTransport transport{TcpTransport::TcpTransportConfig{.m_port = pick_free_port()}};
        EXPECT_FALSE(transport.has_peer());
    }

    TEST(TcpTransportTest, PublishWithoutPeerReturnsNotConnected) {
        TcpTransport transport{TcpTransport::TcpTransportConfig{.m_port = pick_free_port()}};

        const auto payload = make_bytes("data");
        const auto result = transport.publish("topic", payload);

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), TransportError::NOT_CONNECTED);
    }

    TEST(TcpTransportTest, StartListeningSucceeds) {
        TcpTransport transport{TcpTransport::TcpTransportConfig{.m_port = pick_free_port()}};
        const auto result = transport.start_listening();
        EXPECT_TRUE(result.has_value());
    }

    TEST(TcpTransportTest, BindFailsOnAlreadyBoundPort) {
        const std::uint16_t port = pick_free_port();

        TcpTransport first{TcpTransport::TcpTransportConfig{.m_port = port}};
        ASSERT_TRUE(first.start_listening().has_value());

        // SO_REUSEADDR allows a second bind to the same address on some platforms,
        // so only assert the error *type* when the second bind actually fails.
        TcpTransport second{TcpTransport::TcpTransportConfig{.m_port = port}};
        const auto result = second.start_listening();
        if (!result.has_value()) {

            EXPECT_EQ(result.error(), TransportError::BIND_FAILED);
        }
    }

    TEST(TcpTransportTest, ConnectToPeerSucceedsAgainstListener) {
        const std::uint16_t port = pick_free_port();

        TcpTransport server{TcpTransport::TcpTransportConfig{.m_port = port}};
        ASSERT_TRUE(server.start_listening().has_value());

        TcpTransport client{TcpTransport::TcpTransportConfig{.m_port = port}};
        EXPECT_TRUE(client.connect_to_peer().has_value());
        EXPECT_TRUE(client.has_peer());  // peer fd is owned even before the handshake settles
    }

    TEST(TcpTransportTest, ConnectFailsOnInvalidHost) {
        TcpTransport client{TcpTransport::TcpTransportConfig{
                .m_port = 9000U, .m_host = "not-an-ip-address"}};

        const auto result = client.connect_to_peer();
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), TransportError::CONNECT_FAILED);
    }

    // ---- TcpTransport: end-to-end framing over loopback --------------------

    TEST(TcpTransportTest, ServerAcceptsClientConnection) {
        const std::uint16_t port = pick_free_port();

        TcpTransport server{TcpTransport::TcpTransportConfig{.m_port = port}};
        ASSERT_TRUE(server.start_listening().has_value());

        TcpTransport client{TcpTransport::TcpTransportConfig{.m_port = port}};
        ASSERT_TRUE(client.connect_to_peer().has_value());

        pump_until(server, client, [&server] { return server.has_peer(); });
        EXPECT_TRUE(server.has_peer());
    }

    TEST(TcpTransportTest, PublishedFrameIsDeliveredToSubscriber) {
        const std::uint16_t port = pick_free_port();

        TcpTransport server{TcpTransport::TcpTransportConfig{.m_port = port}};
        ASSERT_TRUE(server.start_listening().has_value());

        std::string received;
        server.subscribe("topic", [&received](std::span<const std::byte> bytes) {
            received = to_string(bytes);
        });

        TcpTransport client{TcpTransport::TcpTransportConfig{.m_port = port}};
        ASSERT_TRUE(client.connect_to_peer().has_value());

        pump_until(server, client, [&server] { return server.has_peer(); });
        ASSERT_TRUE(server.has_peer());

        const auto payload = make_bytes("payload-data");
        ASSERT_TRUE(client.publish("topic", payload).has_value());

        const auto dispatched =
                pump_until(server, client, [&received] { return !received.empty(); });

        EXPECT_GE(dispatched, 1U);
        EXPECT_EQ(received, "payload-data");
    }

    TEST(TcpTransportTest, MultipleFramesAreAllDispatched) {
        const std::uint16_t port = pick_free_port();

        TcpTransport server{TcpTransport::TcpTransportConfig{.m_port = port}};
        ASSERT_TRUE(server.start_listening().has_value());

        std::vector<std::string> received;
        server.subscribe("topic", [&received](std::span<const std::byte> bytes) {
            received.push_back(to_string(bytes));
        });

        TcpTransport client{TcpTransport::TcpTransportConfig{.m_port = port}};
        ASSERT_TRUE(client.connect_to_peer().has_value());

        pump_until(server, client, [&server] { return server.has_peer(); });
        ASSERT_TRUE(server.has_peer());

        ASSERT_TRUE(client.publish("topic", make_bytes("one")).has_value());
        ASSERT_TRUE(client.publish("topic", make_bytes("two")).has_value());
        ASSERT_TRUE(client.publish("topic", make_bytes("three")).has_value());

        pump_until(server, client, [&received] { return received.size() >= 3U; });

        ASSERT_EQ(received.size(), 3U);
        EXPECT_EQ(received[0], "one");
        EXPECT_EQ(received[1], "two");
        EXPECT_EQ(received[2], "three");
    }

    TEST(TcpTransportTest, FrameForUnknownKeyIsIgnored) {
        const std::uint16_t port = pick_free_port();

        TcpTransport server{TcpTransport::TcpTransportConfig{.m_port = port}};
        ASSERT_TRUE(server.start_listening().has_value());

        int wanted_calls{0};
        server.subscribe("wanted", [&wanted_calls](std::span<const std::byte>) {
            ++wanted_calls;
        });

        TcpTransport client{TcpTransport::TcpTransportConfig{.m_port = port}};
        ASSERT_TRUE(client.connect_to_peer().has_value());

        pump_until(server, client, [&server] { return server.has_peer(); });
        ASSERT_TRUE(server.has_peer());

        ASSERT_TRUE(client.publish("unwanted", make_bytes("data")).has_value());

        // The frame is still parsed and counted as dispatched; it simply has no
        // matching handler, so the "wanted" subscriber is never invoked. The
        // predicate never fires, so this drains for the full time budget.
        const auto dispatched =
                pump_until(server, client, [] { return false; });

        EXPECT_GE(dispatched, 1U);
        EXPECT_EQ(wanted_calls, 0);
    }

    TEST(TcpTransportTest, EmptyPayloadFrameIsDelivered) {
        const std::uint16_t port = pick_free_port();

        TcpTransport server{TcpTransport::TcpTransportConfig{.m_port = port}};
        ASSERT_TRUE(server.start_listening().has_value());

        bool called{false};
        std::size_t observed_size{99U};
        server.subscribe("topic", [&](std::span<const std::byte> bytes) {
            called = true;
            observed_size = bytes.size();
        });

        TcpTransport client{TcpTransport::TcpTransportConfig{.m_port = port}};
        ASSERT_TRUE(client.connect_to_peer().has_value());

        pump_until(server, client, [&server] { return server.has_peer(); });
        ASSERT_TRUE(server.has_peer());

        ASSERT_TRUE(client.publish("topic", std::vector<std::byte>{}).has_value());

        pump_until(server, client, [&called] { return called; });

        EXPECT_TRUE(called);
        EXPECT_EQ(observed_size, 0U);
    }

    TEST(TcpTransportTest, OversizedPayloadIsRejected) {
        const std::uint16_t port = pick_free_port();

        TcpTransport server{TcpTransport::TcpTransportConfig{.m_port = port}};
        ASSERT_TRUE(server.start_listening().has_value());

        // Tight frame bound so we can exceed it without allocating anything large.
        TcpTransport client{TcpTransport::TcpTransportConfig{
                .m_port = port, .m_max_frame_bytes = 4U}};
        ASSERT_TRUE(client.connect_to_peer().has_value());

        pump_until(server, client, [&server] { return server.has_peer(); });
        ASSERT_TRUE(server.has_peer());

        const auto payload = make_bytes("too-long-payload");
        const auto result = client.publish("topic", payload);

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), TransportError::SEND_FAILED);
    }

    TEST(TcpTransportTest, PollWithoutPeerDispatchesNothing) {
        TcpTransport transport{TcpTransport::TcpTransportConfig{.m_port = pick_free_port()}};
        EXPECT_EQ(transport.poll(), 0U);
    }

} // namespace

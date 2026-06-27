//
// Created by Anıl Orhun Demiroğlu.
//

#pragma once


#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <concepts>
#include <type_traits>

#include "ErrorTypes.hpp"


namespace lib::process_communications {

    template<typename T>
    concept Transport = requires(
            T transport,
            std::string_view key,
            std::span<const std::byte> bytes,
            std::function<void(std::span<const std::byte>)> handler) {
        { transport.subscribe(key, handler) } -> std::same_as<void>;
        {
        transport.publish(key, bytes)
        }
        -> std::same_as<std::expected<void, error_types::TransportError>>;
    };

} // namespace lib::process_communications

namespace lib::process_communications::tcp_transport {

    using TransportError = error_types::TransportError;

    class FdHandle {
    public:
        FdHandle() = default;

        explicit FdHandle(int fd) noexcept:
                m_fd{fd} {}

        ~FdHandle() {
            close();
        }

        FdHandle(const FdHandle &) = delete;

        auto operator=(const FdHandle &) -> FdHandle & = delete;

        FdHandle(FdHandle &&other) noexcept:
                m_fd{other.m_fd} {
            other.m_fd = -1;
        }

        auto operator=(FdHandle &&other) noexcept -> FdHandle & {
            if (this != &other) {

                close();
                m_fd = other.m_fd;
                other.m_fd = -1;
            }

            return *this;
        }

        [[nodiscard]] auto get() const noexcept -> int {
            return m_fd;
        }

        [[nodiscard]] auto valid() const noexcept -> bool {
            return m_fd >= 0;
        }

        auto reset(int fd = -1) noexcept -> void {
            close();
            m_fd = fd;
        }

        auto close() noexcept -> void {
            if (m_fd >= 0) {

                ::close(m_fd);
                m_fd = -1;
            }
        }

    private:
        int m_fd{-1};
    };

    class TcpTransport {
    public:
        using Handler = std::function<void(std::span<const std::byte>)>;

        struct TcpTransportConfig {
            std::uint16_t m_port{0U};
            std::string m_host{"127.0.0.1"};         // peer address in client mode
            std::size_t m_max_frame_bytes{1U << 24};  // sanity bound on key/payload sizes
        };

        TcpTransport() = delete;

        explicit TcpTransport(TcpTransportConfig config) :
                m_config{std::move(config)} {
        }

        ~TcpTransport() = default;

        TcpTransport(const TcpTransport &) = delete;

        auto operator=(const TcpTransport &) -> TcpTransport & = delete;

        TcpTransport(TcpTransport &&) = delete;

        auto operator=(TcpTransport &&) -> TcpTransport & = delete;

        auto subscribe(std::string_view key, Handler handler) -> void {
            m_subscribers[std::string{key}].push_back(std::move(handler));
        }

        [[nodiscard]] auto start_listening() -> std::expected<void, TransportError> {
            const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {

                return std::unexpected{TransportError::SOCKET_CREATE_FAILED};
            }

            FdHandle listener{fd};

            int opt{1};
            ::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            if (!set_nonblocking(listener.get())) {

                return std::unexpected{TransportError::SET_NONBLOCKING_FAILED};
            }

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons(m_config.m_port);

            if (::bind(listener.get(), reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {

                return std::unexpected{TransportError::BIND_FAILED};
            }

            if (::listen(listener.get(), sc_listen_backlog) < 0) {

                return std::unexpected{TransportError::LISTEN_FAILED};
            }

            m_listener = std::move(listener);
            return {};
        }

        [[nodiscard]] auto connect_to_peer() -> std::expected<void, TransportError> {
            const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {

                return std::unexpected{TransportError::SOCKET_CREATE_FAILED};
            }

            FdHandle peer{fd};

            if (!set_nonblocking(peer.get())) {

                return std::unexpected{TransportError::SET_NONBLOCKING_FAILED};
            }

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(m_config.m_port);

            if (::inet_pton(AF_INET, m_config.m_host.c_str(), &address.sin_addr) <= 0) {

                return std::unexpected{TransportError::CONNECT_FAILED};
            }

            const int rc = ::connect(peer.get(), reinterpret_cast<sockaddr *>(&address), sizeof(address));
            if (rc < 0 && errno != EINPROGRESS && errno != EWOULDBLOCK) {

                return std::unexpected{TransportError::CONNECT_FAILED};
            }

            m_peer = std::move(peer);
            return {};
        }


        [[nodiscard]] auto publish(
                std::string_view key,
                std::span<const std::byte> bytes) -> std::expected<void, TransportError> {
            if (!m_peer.valid()) {

                return std::unexpected{TransportError::NOT_CONNECTED};
            }

            if (key.size() > m_config.m_max_frame_bytes || bytes.size() > m_config.m_max_frame_bytes) {

                return std::unexpected{TransportError::SEND_FAILED};
            }

            encode_frame(m_outbox, key, bytes);
            flush_outbox();
            return {};
        }


        [[nodiscard]] auto poll() -> std::size_t {
            try_accept();
            drain_socket();
            const std::size_t dispatched = parse_inbox();
            flush_outbox();
            return dispatched;
        }

        [[nodiscard]] auto has_peer() const noexcept -> bool {
            return m_peer.valid();
        }

    private:
        static auto set_nonblocking(int fd) -> bool {
            const int flags = ::fcntl(fd, F_GETFL, 0);
            if (flags < 0) {

                return false;
            }

            return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
        }

        static auto put_u32_le(std::vector<std::byte> &out, std::uint32_t value) -> void {
            out.push_back(static_cast<std::byte>(value & 0xFFU));
            out.push_back(static_cast<std::byte>((value >> 8) & 0xFFU));
            out.push_back(static_cast<std::byte>((value >> 16) & 0xFFU));
            out.push_back(static_cast<std::byte>((value >> 24) & 0xFFU));
        }

        static auto load_u32_le(std::span<const std::byte> in) -> std::uint32_t {
            return static_cast<std::uint32_t>(in[0])
                   | (static_cast<std::uint32_t>(in[1]) << 8)
                   | (static_cast<std::uint32_t>(in[2]) << 16)
                   | (static_cast<std::uint32_t>(in[3]) << 24);
        }

        static auto encode_frame(
                std::vector<std::byte> &out,
                std::string_view key,
                std::span<const std::byte> payload) -> void {
            put_u32_le(out, static_cast<std::uint32_t>(key.size()));
            put_u32_le(out, static_cast<std::uint32_t>(payload.size()));

            const auto *key_bytes = reinterpret_cast<const std::byte *>(key.data());
            out.insert(out.end(), key_bytes, key_bytes + key.size());
            out.insert(out.end(), payload.begin(), payload.end());
        }

        auto try_accept() -> void {
            if (!m_listener.valid() || m_peer.valid()) {

                return;
            }

            const int fd = ::accept(m_listener.get(), nullptr, nullptr);
            if (fd < 0) {

                return;  // EAGAIN/EWOULDBLOCK — no pending connection this tick
            }

            if (!set_nonblocking(fd)) {

                ::close(fd);
                return;
            }

            m_peer.reset(fd);
        }

        auto drain_socket() -> void {
            if (!m_peer.valid()) {

                return;
            }

            std::array<std::byte, sc_recv_chunk> chunk{};

            while (true) {

                const auto received = ::recv(m_peer.get(), chunk.data(), chunk.size(), 0);

                if (received > 0) {

                    m_inbox.insert(
                            m_inbox.end(),
                            chunk.begin(),
                            chunk.begin() + received);
                    continue;
                }

                if (received == 0) {

                    reset_peer();  // peer performed an orderly shutdown
                    return;
                }

                if (errno == EINTR) {

                    continue;
                }

                if (errno == EAGAIN || errno == EWOULDBLOCK) {

                    return;  // socket drained for now
                }

                reset_peer();  // hard receive error
                return;
            }
        }

        auto parse_inbox() -> std::size_t {
            std::size_t consumed{0U};
            std::size_t dispatched{0U};

            while (m_inbox.size() - consumed >= sc_header_bytes) {

                const auto header =
                        std::span<const std::byte>{m_inbox}.subspan(consumed, sc_header_bytes);

                const auto key_size = load_u32_le(header.subspan(0, 4));
                const auto payload_size = load_u32_le(header.subspan(4, 4));

                if (key_size > m_config.m_max_frame_bytes
                    || payload_size > m_config.m_max_frame_bytes) {

                    reset_peer();  // framing desync / corrupt stream — drop the connection
                    m_inbox.clear();
                    return dispatched;
                }

                const std::size_t frame_size =
                        sc_header_bytes + key_size + payload_size;

                if (m_inbox.size() - consumed < frame_size) {

                    break;  // partial frame — wait for more bytes
                }

                const auto body =
                        std::span<const std::byte>{m_inbox}.subspan(
                                consumed + sc_header_bytes,
                                key_size + payload_size);

                std::string key(key_size, '\0');
                std::memcpy(key.data(), body.data(), key_size);

                const auto payload = body.subspan(key_size, payload_size);

                dispatch(key, payload);

                consumed += frame_size;
                ++dispatched;
            }

            if (consumed > 0U) {

                m_inbox.erase(
                        m_inbox.begin(),
                        m_inbox.begin() + static_cast<std::ptrdiff_t>(consumed));
            }

            return dispatched;
        }

        auto flush_outbox() -> void {
            if (!m_peer.valid() || m_outbox.empty()) {

                return;
            }

            std::size_t sent_total{0U};

            while (sent_total < m_outbox.size()) {

                const auto sent = ::send(
                        m_peer.get(),
                        m_outbox.data() + sent_total,
                        m_outbox.size() - sent_total,
                        MSG_NOSIGNAL);

                if (sent > 0) {

                    sent_total += static_cast<std::size_t>(sent);
                    continue;
                }

                if (sent < 0 && errno == EINTR) {

                    continue;
                }

                if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {

                    break;  // kernel buffer full — remainder stays queued for next poll()
                }

                reset_peer();  // hard send error
                m_outbox.clear();
                return;
            }

            if (sent_total > 0U) {

                m_outbox.erase(
                        m_outbox.begin(),
                        m_outbox.begin() + static_cast<std::ptrdiff_t>(sent_total));
            }
        }

        auto dispatch(const std::string &key, std::span<const std::byte> bytes) -> void {
            const auto it = m_subscribers.find(key);

            if (it == m_subscribers.end()) {

                return;
            }

            for (const auto &handler: it->second) {

                handler(bytes);
            }
        }

        auto reset_peer() -> void {
            m_peer.reset();
            m_inbox.clear();
            m_outbox.clear();
        }

        static constexpr std::size_t sc_header_bytes{8U};       // u32 key_size + u32 payload_size
        static constexpr std::size_t sc_recv_chunk{65536U};     // per-recv read granularity
        static constexpr int sc_listen_backlog{4};

        // (1) config, (2) connection state, (3) buffers, (4) subscriptions (§15 ordering).
        TcpTransportConfig m_config;
        FdHandle m_listener{};
        FdHandle m_peer{};
        std::vector<std::byte> m_inbox{};
        std::vector<std::byte> m_outbox{};
        std::unordered_map<std::string, std::vector<Handler>> m_subscribers{};
    };

    static_assert(Transport<TcpTransport>);

} // namespace lib::process_communications::tcp_transport
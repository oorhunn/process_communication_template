//
// Created by Anıl Orhun Demiroğlu.
//

#pragma once


#include <unordered_map>
#include <vector>
#include <functional>
#include <span>
#include <expected>

#include "ErrorTypes.hpp"

namespace common::process_communications::inproc_transport {


    struct InprocTransportConfig {
    };

    class InprocTransport {

        using Handler = std::function<void(std::span<const std::byte>)>;

    public:

        InprocTransport() = delete;

        InprocTransport(const InprocTransport &) = delete;

        InprocTransport(InprocTransport &&) = delete;

        auto operator=(const InprocTransport &) -> InprocTransport & = delete;

        auto operator=(InprocTransport &&) -> InprocTransport & = delete;

        explicit InprocTransport(const InprocTransportConfig &config) : m_config{config} {}


        auto subscribe(std::string_view key, Handler handler) -> void {

            m_subscribers[std::string{key}].push_back(std::move(handler));
        }

        [[nodiscard]] auto publish(
                std::string_view key,
                std::span<const std::byte> bytes) ->
        std::expected<void, common::process_communications::error_types::TransportError> {

            const auto it{m_subscribers.find(std::string{key})};
            if (it != m_subscribers.end()) {

                for (const auto &handler: it->second) {

                    handler(bytes);
                }
            }
            return {};
        }

    private:
        InprocTransportConfig m_config;
        std::unordered_map<std::string, std::vector<Handler>> m_subscribers{};

    };

}
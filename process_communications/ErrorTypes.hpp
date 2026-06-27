//
// Created by Anıl Orhun Demiroğlu.
//

#pragma once

#include <cstdint>

namespace lib::process_communications::error_types {

    enum class TransportError : std::uint8_t {
        PUBLISH_FAILED = 0,
        NOT_CONNECTED,
        QUEUE_FULL,
        SOCKET_CREATE_FAILED,
        SET_NONBLOCKING_FAILED,
        BIND_FAILED,
        LISTEN_FAILED,
        CONNECT_FAILED,
        SEND_FAILED,
    };

}
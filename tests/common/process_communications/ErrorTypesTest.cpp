//
// Tests for common/process_communications/ErrorTypes.hpp
//

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include "common/process_communications/ErrorTypes.hpp"

namespace {

    using common::process_communications::error_types::TransportError;

    TEST(ErrorTypesTest, UnderlyingTypeIsUint8) {
        static_assert(
                std::is_same_v<std::underlying_type_t<TransportError>, std::uint8_t>,
                "TransportError must be backed by std::uint8_t");
        SUCCEED();
    }

    TEST(ErrorTypesTest, EnumeratorsHaveExpectedValues) {
        // The numeric values are part of the wire/contract surface, so pin them down.
        EXPECT_EQ(static_cast<std::uint8_t>(TransportError::PUBLISH_FAILED), 0U);
        EXPECT_EQ(static_cast<std::uint8_t>(TransportError::NOT_CONNECTED), 1U);
        EXPECT_EQ(static_cast<std::uint8_t>(TransportError::QUEUE_FULL), 2U);
        EXPECT_EQ(static_cast<std::uint8_t>(TransportError::SOCKET_CREATE_FAILED), 3U);
        EXPECT_EQ(static_cast<std::uint8_t>(TransportError::SET_NONBLOCKING_FAILED), 4U);
        EXPECT_EQ(static_cast<std::uint8_t>(TransportError::BIND_FAILED), 5U);
        EXPECT_EQ(static_cast<std::uint8_t>(TransportError::LISTEN_FAILED), 6U);
        EXPECT_EQ(static_cast<std::uint8_t>(TransportError::CONNECT_FAILED), 7U);
        EXPECT_EQ(static_cast<std::uint8_t>(TransportError::SEND_FAILED), 8U);
    }

    TEST(ErrorTypesTest, EnumeratorsAreDistinct) {
        EXPECT_NE(TransportError::NOT_CONNECTED, TransportError::SEND_FAILED);
        EXPECT_NE(TransportError::BIND_FAILED, TransportError::LISTEN_FAILED);
    }

} // namespace

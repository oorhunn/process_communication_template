//
// Tests for lib/process_communications/InprocTransport.hpp
//

#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "process_communications/ErrorTypes.hpp"
#include "process_communications/InprocTransport.hpp"

namespace {

    using lib::process_communications::error_types::TransportError;
    using lib::process_communications::inproc_transport::InprocTransport;
    using lib::process_communications::inproc_transport::InprocTransportConfig;

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

    TEST(InprocTransportTest, PublishWithoutSubscribersSucceeds) {
        InprocTransport transport{InprocTransportConfig{}};

        const auto payload = make_bytes("hello");
        const auto result = transport.publish("topic", payload);

        EXPECT_TRUE(result.has_value());
    }

    TEST(InprocTransportTest, SubscriberReceivesPublishedPayload) {
        InprocTransport transport{InprocTransportConfig{}};

        std::string received;
        transport.subscribe("topic", [&received](std::span<const std::byte> bytes) {
            received = to_string(bytes);
        });

        const auto payload = make_bytes("payload-data");
        const auto result = transport.publish("topic", payload);

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(received, "payload-data");
    }

    TEST(InprocTransportTest, OnlyMatchingKeyIsDispatched) {
        InprocTransport transport{InprocTransportConfig{}};

        int matching_calls{0};
        int other_calls{0};
        transport.subscribe("wanted", [&matching_calls](std::span<const std::byte>) {
            ++matching_calls;
        });
        transport.subscribe("unwanted", [&other_calls](std::span<const std::byte>) {
            ++other_calls;
        });

        const auto payload = make_bytes("x");
        ASSERT_TRUE(transport.publish("wanted", payload).has_value());

        EXPECT_EQ(matching_calls, 1);
        EXPECT_EQ(other_calls, 0);
    }

    TEST(InprocTransportTest, MultipleSubscribersOnSameKeyAllReceive) {
        InprocTransport transport{InprocTransportConfig{}};

        int first{0};
        int second{0};
        transport.subscribe("topic", [&first](std::span<const std::byte>) { ++first; });
        transport.subscribe("topic", [&second](std::span<const std::byte>) { ++second; });

        const auto payload = make_bytes("data");
        ASSERT_TRUE(transport.publish("topic", payload).has_value());

        EXPECT_EQ(first, 1);
        EXPECT_EQ(second, 1);
    }

    TEST(InprocTransportTest, HandlerInvokedOncePerPublish) {
        InprocTransport transport{InprocTransportConfig{}};

        int calls{0};
        transport.subscribe("topic", [&calls](std::span<const std::byte>) { ++calls; });

        const auto payload = make_bytes("data");
        ASSERT_TRUE(transport.publish("topic", payload).has_value());
        ASSERT_TRUE(transport.publish("topic", payload).has_value());
        ASSERT_TRUE(transport.publish("topic", payload).has_value());

        EXPECT_EQ(calls, 3);
    }

    TEST(InprocTransportTest, EmptyPayloadIsDelivered) {
        InprocTransport transport{InprocTransportConfig{}};

        bool called{false};
        std::size_t observed_size{42U};
        transport.subscribe("topic", [&](std::span<const std::byte> bytes) {
            called = true;
            observed_size = bytes.size();
        });

        const std::vector<std::byte> empty;
        ASSERT_TRUE(transport.publish("topic", empty).has_value());

        EXPECT_TRUE(called);
        EXPECT_EQ(observed_size, 0U);
    }

} // namespace

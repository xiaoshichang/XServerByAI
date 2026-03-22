#include "GmInnerService.h"
#include "message/HeartbeatCodec.h"
#include "InnerNetwork.h"
#include "message/PacketCodec.h"
#include "TimeUtils.h"
#include "ZmqActiveConnector.h"
#include "ZmqContext.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

int g_failures = 0;

void Check(bool condition, const char* expression, const char* message = nullptr)
{
    if (condition)
    {
        return;
    }

    std::cerr << "Check failed: " << expression;
    if (message != nullptr)
    {
        std::cerr << " (" << message << ")";
    }
    std::cerr << '\n';
    ++g_failures;
}

#define XS_CHECK(expr) Check((expr), #expr)
#define XS_CHECK_MSG(expr, message) Check((expr), #expr, (message))

struct RoutedResponse
{
    std::string label{};
    std::vector<std::byte> payload{};
};

[[nodiscard]] std::filesystem::path PrepareTestDirectory(std::string_view name)
{
    const std::filesystem::path path = std::filesystem::current_path() / "test-output" / std::string(name);
    std::error_code error_code;
    std::filesystem::remove_all(path, error_code);
    std::filesystem::create_directories(path, error_code);
    return path;
}

void CleanupTestDirectory(const std::filesystem::path& path)
{
    std::error_code error_code;
    std::filesystem::remove_all(path, error_code);
}

[[nodiscard]] xs::node::RoutingID MakeRoutingId(std::string_view value)
{
    xs::node::RoutingID routing_id;
    routing_id.reserve(value.size());
    for (const char ch : value)
    {
        routing_id.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }

    return routing_id;
}

[[nodiscard]] xs::net::LoadSnapshot MakeLoadSnapshot(
    std::uint32_t connection_count,
    std::uint32_t session_count,
    std::uint32_t entity_count,
    std::uint32_t space_count,
    std::uint32_t load_score)
{
    return xs::net::LoadSnapshot{
        .connection_count = connection_count,
        .session_count = session_count,
        .entity_count = entity_count,
        .space_count = space_count,
        .load_score = load_score,
    };
}

[[nodiscard]] std::uint64_t CurrentUnixTimeMilliseconds()
{
    const std::int64_t now_unix_ms = xs::core::ToUnixTimeMilliseconds(xs::core::UtcNow());
    return now_unix_ms > 0 ? static_cast<std::uint64_t>(now_unix_ms) : 0U;
}

[[nodiscard]] xs::node::ProcessRegistryRegistration MakeRegistration(
    std::string node_id,
    std::string routing_id,
    std::uint64_t last_heartbeat_at_unix_ms)
{
    return xs::node::ProcessRegistryRegistration{
        .process_type = static_cast<std::uint16_t>(xs::net::InnerProcessType::Game),
        .node_id = std::move(node_id),
        .pid = 1001U,
        .started_at_unix_ms = CurrentUnixTimeMilliseconds(),
        .inner_network_endpoint =
            xs::net::Endpoint{
                .host = "127.0.0.1",
                .port = 7100U,
            },
        .build_version = "test-build",
        .capability_tags = {"heartbeat"},
        .load = MakeLoadSnapshot(1U, 2U, 3U, 4U, 5U),
        .routing_id = MakeRoutingId(routing_id),
        .last_heartbeat_at_unix_ms = last_heartbeat_at_unix_ms,
        .inner_network_ready = false,
    };
}

[[nodiscard]] xs::core::LoggerOptions MakeLoggerOptions(
    const std::filesystem::path& root_dir,
    std::string instance_id)
{
    xs::core::LoggerOptions options;
    options.process_type = xs::core::ProcessType::Gm;
    options.instance_id = std::move(instance_id);
    options.config.root_dir = root_dir.string();
    options.config.min_level = xs::core::LogLevel::Info;
    return options;
}

[[nodiscard]] std::vector<std::byte> EncodeHeartbeatPacket(
    std::uint32_t seq,
    std::uint16_t packet_flags,
    std::uint32_t status_flags,
    const xs::net::LoadSnapshot& load)
{
    const xs::net::HeartbeatRequest request{
        .sent_at_unix_ms = CurrentUnixTimeMilliseconds(),
        .status_flags = status_flags,
        .load = load,
    };

    std::array<std::byte, xs::net::kHeartbeatRequestSize> body{};
    XS_CHECK(xs::net::EncodeHeartbeatRequest(request, body) == xs::net::HeartbeatCodecErrorCode::None);

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + body.size());
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerHeartbeatMsgId,
        seq,
        packet_flags,
        static_cast<std::uint32_t>(body.size()));
    XS_CHECK(xs::net::EncodePacket(header, body, packet) == xs::net::PacketCodecErrorCode::None);
    return packet;
}

void CheckHeartbeatSuccessResponse(
    const std::vector<std::byte>& payload,
    std::uint32_t expected_seq,
    std::uint32_t expected_interval_ms,
    std::uint32_t expected_timeout_ms)
{
    xs::net::PacketView packet{};
    XS_CHECK(xs::net::DecodePacket(payload, &packet) == xs::net::PacketCodecErrorCode::None);
    XS_CHECK(packet.header.msg_id == xs::net::kInnerHeartbeatMsgId);
    XS_CHECK(packet.header.seq == expected_seq);
    XS_CHECK(packet.header.flags == static_cast<std::uint16_t>(xs::net::PacketFlag::Response));

    xs::net::HeartbeatSuccessResponse response{};
    XS_CHECK(
        xs::net::DecodeHeartbeatSuccessResponse(packet.payload, &response) ==
        xs::net::HeartbeatCodecErrorCode::None);
    XS_CHECK(response.heartbeat_interval_ms == expected_interval_ms);
    XS_CHECK(response.heartbeat_timeout_ms == expected_timeout_ms);
    XS_CHECK(response.server_now_unix_ms > 0U);
}

void CheckHeartbeatErrorResponse(
    const std::vector<std::byte>& payload,
    std::uint32_t expected_seq,
    std::int32_t expected_error_code,
    bool expected_require_full_register)
{
    xs::net::PacketView packet{};
    XS_CHECK(xs::net::DecodePacket(payload, &packet) == xs::net::PacketCodecErrorCode::None);
    XS_CHECK(packet.header.msg_id == xs::net::kInnerHeartbeatMsgId);
    XS_CHECK(packet.header.seq == expected_seq);
    XS_CHECK(
        packet.header.flags ==
        (static_cast<std::uint16_t>(xs::net::PacketFlag::Response) |
         static_cast<std::uint16_t>(xs::net::PacketFlag::Error)));

    xs::net::HeartbeatErrorResponse response{};
    XS_CHECK(
        xs::net::DecodeHeartbeatErrorResponse(packet.payload, &response) ==
        xs::net::HeartbeatCodecErrorCode::None);
    XS_CHECK(response.error_code == expected_error_code);
    XS_CHECK(response.retry_after_ms == 0U);
    XS_CHECK(response.require_full_register == expected_require_full_register);
}

void TestTimeoutScanEvictsExpiredEntry()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-inner-service-timeout");
    const std::filesystem::path log_dir = base_path / "logs" / "gm";

    xs::core::Logger logger(MakeLoggerOptions(log_dir, "GM"));
    xs::core::MainEventLoop event_loop({.thread_name = "gm-inner-timeout"});
    xs::node::InnerNetwork inner_network(event_loop, logger, {});
    xs::node::GmInnerService service(
        event_loop,
        logger,
        inner_network,
        {
            .heartbeat_interval_ms = 20U,
            .heartbeat_timeout_ms = 30U,
            .timeout_scan_interval = std::chrono::milliseconds(5),
            .invalidated_routing_retention = std::chrono::milliseconds(200),
        });

    const std::uint64_t now_unix_ms = CurrentUnixTimeMilliseconds();
    const xs::node::ProcessRegistryRegistration expired_registration =
        MakeRegistration("Game0", "route-expired", now_unix_ms - 100U);

    xs::core::MainEventLoopHooks hooks;
    hooks.on_start = [&](xs::core::MainEventLoop& running_loop, std::string* error_message) {
        if (inner_network.Init() != xs::node::NodeErrorCode::None ||
            inner_network.Run() != xs::node::NodeErrorCode::None ||
            service.Init() != xs::node::NodeErrorCode::None ||
            service.RegisterProcess(expired_registration) != xs::node::ProcessRegistryErrorCode::None ||
            service.Run() != xs::node::NodeErrorCode::None)
        {
            if (error_message != nullptr)
            {
                *error_message = "Failed to initialize timeout scan test.";
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        const xs::core::TimerCreateResult stop_timer =
            running_loop.timers().CreateOnce(std::chrono::milliseconds(60), [&running_loop]() {
                running_loop.RequestStop();
            });
        if (!xs::core::IsTimerID(stop_timer))
        {
            if (error_message != nullptr)
            {
                *error_message = "Failed to create timeout test stop timer.";
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        return xs::core::MainEventLoopErrorCode::None;
    };
    hooks.on_stop = [&](xs::core::MainEventLoop&) {
        (void)service.Uninit();
        (void)inner_network.Uninit();
    };

    std::string error_message;
    const xs::core::MainEventLoopErrorCode run_result = event_loop.Run(std::move(hooks), &error_message);
    XS_CHECK_MSG(run_result == xs::core::MainEventLoopErrorCode::None, error_message.c_str());

    XS_CHECK(!service.process_registry().ContainsNodeId("Game0"));
    XS_CHECK(service.ContainsInvalidatedRoutingId(MakeRoutingId("route-expired")));

    CleanupTestDirectory(base_path);
}

void TestHeartbeatResponsesOverInnerNetwork()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-inner-service-network");
    const std::filesystem::path log_dir = base_path / "logs" / "gm";

    xs::core::Logger logger(MakeLoggerOptions(log_dir, "GM"));
    xs::core::MainEventLoop event_loop({.thread_name = "gm-inner-network"});

    xs::node::InnerNetworkOptions network_options;
    network_options.mode = xs::node::InnerNetworkMode::PassiveListener;
    network_options.local_endpoint = "tcp://127.0.0.1:*";

    xs::node::InnerNetwork inner_network(event_loop, logger, std::move(network_options));
    xs::node::GmInnerService service(
        event_loop,
        logger,
        inner_network,
        {
            .heartbeat_interval_ms = 40U,
            .heartbeat_timeout_ms = 5000U,
            .timeout_scan_interval = std::chrono::milliseconds(10),
            .invalidated_routing_retention = std::chrono::milliseconds(5000),
        });

    std::shared_ptr<xs::ipc::ZmqContext> connector_context;
    std::vector<std::shared_ptr<xs::ipc::ZmqActiveConnector>> connectors;
    std::vector<bool> sent_flags;
    std::vector<RoutedResponse> responses;
    std::string bound_endpoint;

    const std::vector<std::string> labels{"active", "unknown", "invalid", "malformed"};
    const std::vector<std::string> routing_ids{"route-active", "route-unknown", "route-invalid", "route-bad"};
    const std::vector<std::vector<std::byte>> packets{
        EncodeHeartbeatPacket(11U, 0U, 0U, MakeLoadSnapshot(10U, 11U, 12U, 13U, 14U)),
        EncodeHeartbeatPacket(22U, 0U, 0U, MakeLoadSnapshot(20U, 21U, 22U, 23U, 24U)),
        EncodeHeartbeatPacket(33U, 0U, 0U, MakeLoadSnapshot(30U, 31U, 32U, 33U, 34U)),
        EncodeHeartbeatPacket(0U, 0U, 0U, MakeLoadSnapshot(40U, 41U, 42U, 43U, 44U)),
    };

    xs::core::MainEventLoopHooks hooks;
    hooks.on_start = [&](xs::core::MainEventLoop& running_loop, std::string* error_message) {
        if (inner_network.Init() != xs::node::NodeErrorCode::None ||
            service.Init() != xs::node::NodeErrorCode::None ||
            inner_network.Run() != xs::node::NodeErrorCode::None ||
            service.RegisterProcess(MakeRegistration("Game0", "route-active", CurrentUnixTimeMilliseconds())) !=
                xs::node::ProcessRegistryErrorCode::None ||
            service.Run() != xs::node::NodeErrorCode::None)
        {
            if (error_message != nullptr)
            {
                *error_message = "Failed to initialize GM inner network test.";
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        service.InvalidateRoutingId(MakeRoutingId("route-invalid"));
        bound_endpoint = std::string(inner_network.bound_endpoint());

        connector_context = std::make_shared<xs::ipc::ZmqContext>();
        if (!connector_context->IsValid())
        {
            if (error_message != nullptr)
            {
                *error_message = "Failed to initialize ZeroMQ context for GM inner network test.";
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        connectors.reserve(labels.size());
        sent_flags.assign(labels.size(), false);
        for (std::size_t index = 0; index < labels.size(); ++index)
        {
            xs::ipc::ZmqActiveConnectorOptions connector_options;
            connector_options.remote_endpoint = bound_endpoint;
            connector_options.routing_id = routing_ids[index];

            auto connector = std::make_shared<xs::ipc::ZmqActiveConnector>(
                running_loop.context(),
                *connector_context,
                std::move(connector_options));
            connector->SetMessageHandler([&, index](std::vector<std::byte> payload) {
                responses.push_back(RoutedResponse{
                    .label = labels[index],
                    .payload = std::move(payload),
                });
            });
            connector->SetStateHandler([&, index, connector](xs::ipc::ZmqConnectionState state) {
                if (state != xs::ipc::ZmqConnectionState::Connected || sent_flags[index])
                {
                    return;
                }

                sent_flags[index] = true;
                const xs::core::TimerCreateResult send_timer =
                    running_loop.timers().CreateOnce(std::chrono::milliseconds(10), [&, index, connector]() {
                        std::string local_error;
                        const xs::ipc::ZmqSocketErrorCode send_result = connector->Send(packets[index], &local_error);
                        XS_CHECK_MSG(send_result == xs::ipc::ZmqSocketErrorCode::None, local_error.c_str());
                    });
                XS_CHECK(xs::core::IsTimerID(send_timer));
            });

            std::string connector_error;
            const xs::ipc::ZmqSocketErrorCode start_result = connector->Start(&connector_error);
            XS_CHECK_MSG(start_result == xs::ipc::ZmqSocketErrorCode::None, connector_error.c_str());
            connectors.push_back(std::move(connector));
        }

        const xs::core::TimerCreateResult poll_timer =
            running_loop.timers().CreateRepeating(std::chrono::milliseconds(5), [&running_loop, &responses, expected = labels.size()]() {
                if (responses.size() >= expected)
                {
                    running_loop.RequestStop();
                }
            });
        if (!xs::core::IsTimerID(poll_timer))
        {
            if (error_message != nullptr)
            {
                *error_message = "Failed to create GM inner response poll timer.";
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        const xs::core::TimerCreateResult timeout_timer =
            running_loop.timers().CreateOnce(std::chrono::milliseconds(1500), [&running_loop]() {
                running_loop.RequestStop();
            });
        if (!xs::core::IsTimerID(timeout_timer))
        {
            if (error_message != nullptr)
            {
                *error_message = "Failed to create GM inner timeout timer.";
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        return xs::core::MainEventLoopErrorCode::None;
    };
    hooks.on_stop = [&](xs::core::MainEventLoop&) {
        for (const auto& connector : connectors)
        {
            connector->Stop();
        }

        (void)service.Uninit();
        (void)inner_network.Uninit();
    };

    std::string error_message;
    const xs::core::MainEventLoopErrorCode run_result = event_loop.Run(std::move(hooks), &error_message);
    XS_CHECK_MSG(run_result == xs::core::MainEventLoopErrorCode::None, error_message.c_str());
    XS_CHECK(responses.size() == labels.size());

    for (const RoutedResponse& response : responses)
    {
        if (response.label == "active")
        {
            CheckHeartbeatSuccessResponse(response.payload, 11U, 40U, 5000U);
            continue;
        }

        if (response.label == "unknown")
        {
            CheckHeartbeatErrorResponse(response.payload, 22U, 3003, true);
            continue;
        }

        if (response.label == "invalid")
        {
            CheckHeartbeatErrorResponse(response.payload, 33U, 3004, true);
            continue;
        }

        if (response.label == "malformed")
        {
            CheckHeartbeatErrorResponse(response.payload, 0U, 3005, false);
            continue;
        }

        XS_CHECK_MSG(false, response.label.c_str());
    }

    const xs::node::ProcessRegistryEntry* active_entry = service.process_registry().FindByNodeId("Game0");
    XS_CHECK(active_entry != nullptr);
    if (active_entry != nullptr)
    {
        XS_CHECK(active_entry->load.connection_count == 10U);
        XS_CHECK(active_entry->load.session_count == 11U);
        XS_CHECK(active_entry->load.entity_count == 12U);
        XS_CHECK(active_entry->load.space_count == 13U);
        XS_CHECK(active_entry->load.load_score == 14U);
    }

    CleanupTestDirectory(base_path);
}

} // namespace

int main()
{
    TestTimeoutScanEvictsExpiredEntry();
    TestHeartbeatResponsesOverInnerNetwork();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " gm inner service test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

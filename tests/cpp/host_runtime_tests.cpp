#include "ManagedRuntimeHost.h"
#include "Timer.h"
#include "message/RelayCodec.h"

#include <asio/io_context.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifndef XS_TEST_MANAGED_FRAMEWORK_ASSEMBLY_PATH
#error XS_TEST_MANAGED_FRAMEWORK_ASSEMBLY_PATH must be defined.
#endif

#ifndef XS_TEST_MANAGED_FRAMEWORK_RUNTIMECONFIG_PATH
#error XS_TEST_MANAGED_FRAMEWORK_RUNTIMECONFIG_PATH must be defined.
#endif

#ifndef XS_TEST_MANAGED_GAMELOGIC_ASSEMBLY_PATH
#error XS_TEST_MANAGED_GAMELOGIC_ASSEMBLY_PATH must be defined.
#endif

#ifndef XS_TEST_MANAGED_ABI_MISMATCH_ASSEMBLY_PATH
#error XS_TEST_MANAGED_ABI_MISMATCH_ASSEMBLY_PATH must be defined.
#endif

#ifndef XS_TEST_MANAGED_ABI_MISMATCH_RUNTIMECONFIG_PATH
#error XS_TEST_MANAGED_ABI_MISMATCH_RUNTIMECONFIG_PATH must be defined.
#endif

#ifndef XS_TEST_MANAGED_MISSING_EXPORTS_ASSEMBLY_PATH
#error XS_TEST_MANAGED_MISSING_EXPORTS_ASSEMBLY_PATH must be defined.
#endif

#ifndef XS_TEST_MANAGED_MISSING_EXPORTS_RUNTIMECONFIG_PATH
#error XS_TEST_MANAGED_MISSING_EXPORTS_RUNTIMECONFIG_PATH must be defined.
#endif

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

const std::filesystem::path kManagedAssemblyPath{XS_TEST_MANAGED_FRAMEWORK_ASSEMBLY_PATH};
const std::filesystem::path kManagedRuntimeConfigPath{XS_TEST_MANAGED_FRAMEWORK_RUNTIMECONFIG_PATH};
const std::filesystem::path kManagedGameLogicAssemblyPath{XS_TEST_MANAGED_GAMELOGIC_ASSEMBLY_PATH};
const std::filesystem::path kAbiMismatchAssemblyPath{XS_TEST_MANAGED_ABI_MISMATCH_ASSEMBLY_PATH};
const std::filesystem::path kAbiMismatchRuntimeConfigPath{XS_TEST_MANAGED_ABI_MISMATCH_RUNTIMECONFIG_PATH};
const std::filesystem::path kMissingExportsAssemblyPath{XS_TEST_MANAGED_MISSING_EXPORTS_ASSEMBLY_PATH};
const std::filesystem::path kMissingExportsRuntimeConfigPath{XS_TEST_MANAGED_MISSING_EXPORTS_RUNTIMECONFIG_PATH};

std::string DescribeManagedHostResult(xs::host::ManagedHostErrorCode code)
{
    return std::string(xs::host::ManagedHostErrorCanonicalName(code)) + ": " +
           std::string(xs::host::ManagedHostErrorMessage(code));
}

std::filesystem::path NormalizePath(const std::filesystem::path& path)
{
    std::error_code error_code;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error_code);
    if (!error_code)
    {
        return canonical;
    }

    const std::filesystem::path absolute = std::filesystem::absolute(path, error_code);
    if (!error_code)
    {
        return absolute;
    }

    return path;
}

xs::host::ManagedRuntimeHostOptions MakePrimaryManagedRuntimeHostOptions()
{
    return xs::host::ManagedRuntimeHostOptions{
        kManagedRuntimeConfigPath,
        kManagedAssemblyPath,
        {
            kManagedAssemblyPath,
            kManagedGameLogicAssemblyPath,
        },
    };
}

std::string ReadManagedUtf8(const std::uint8_t* buffer, std::uint32_t length)
{
    if (buffer == nullptr)
    {
        return {};
    }

    return std::string(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(length));
}

bool IsCanonicalGuidText(std::string_view value)
{
    if (value.size() != 36U)
    {
        return false;
    }

    for (std::size_t index = 0U; index < value.size(); ++index)
    {
        if (index == 8U || index == 13U || index == 18U || index == 23U)
        {
            if (value[index] != '-')
            {
                return false;
            }

            continue;
        }

        if (!std::isxdigit(static_cast<unsigned char>(value[index])))
        {
            return false;
        }
    }

    return true;
}

bool TryWriteManagedUtf8String(std::string_view value, std::span<std::uint8_t> utf8_buffer,
                               std::uint32_t* output_length)
{
    if (output_length == nullptr || value.size() > utf8_buffer.size())
    {
        return false;
    }

    std::fill(utf8_buffer.begin(), utf8_buffer.end(), static_cast<std::uint8_t>(0));
    if (!value.empty())
    {
        std::memcpy(utf8_buffer.data(), value.data(), value.size());
    }

    *output_length = static_cast<std::uint32_t>(value.size());
    return true;
}

struct ManagedInitInput final
{
    std::string node_id{};
    std::string config_path{};
    xs::host::ManagedInitArgs args{};
};

struct ReadyCallbackCapture final
{
    std::uint32_t call_count{0U};
    std::vector<std::uint64_t> assignment_epochs{};
    std::vector<std::string> entity_types{};
    std::vector<std::string> entity_ids{};
};

struct ManagedLogCapture final
{
    std::uint32_t call_count{0U};
    std::vector<std::uint32_t> levels{};
    std::vector<std::string> categories{};
    std::vector<std::string> messages{};
};

struct ManagedNativeTimerCapture final
{
    asio::io_context io_context{};
    xs::core::TimerManager timer_manager;
    const xs::host::ManagedExports* exports{nullptr};
    std::vector<std::uint64_t> requested_create_once_delays_ms{};
    std::vector<std::int64_t> created_timer_ids{};
    std::vector<std::int64_t> fired_timer_ids{};
    std::vector<std::int64_t> cancelled_timer_ids{};

    ManagedNativeTimerCapture() : timer_manager(io_context)
    {
    }

    void RunUntilIdle()
    {
        io_context.restart();
        io_context.run();
    }
};

struct ManagedForwardMailboxCallCapture final
{
    std::uint32_t call_count{0U};
    std::vector<std::string> target_game_node_ids{};
    std::vector<std::string> target_mailbox_names{};
    std::vector<std::uint32_t> msg_ids{};
    std::vector<std::vector<std::byte>> payloads{};
};

struct ManagedForwardProxyCallCapture final
{
    std::uint32_t call_count{0U};
    std::vector<std::string> route_gate_node_ids{};
    std::vector<std::string> target_entity_ids{};
    std::vector<std::uint32_t> msg_ids{};
    std::vector<std::vector<std::byte>> payloads{};
};

struct ManagedPushClientMessageCapture final
{
    std::uint32_t call_count{0U};
    std::vector<std::string> route_gate_node_ids{};
    std::vector<std::string> target_entity_ids{};
    std::vector<std::uint32_t> msg_ids{};
    std::vector<std::vector<std::byte>> payloads{};
};

struct ManagedCallbackCapture final
{
    ReadyCallbackCapture ready{};
    ManagedLogCapture logs{};
    ManagedNativeTimerCapture timers{};
    ManagedForwardMailboxCallCapture forwarded_mailbox_calls{};
    ManagedForwardProxyCallCapture forwarded_proxy_calls{};
    ManagedPushClientMessageCapture pushed_client_messages{};
};

void OnServerStubReady(void* context, std::uint64_t assignment_epoch,
                       const xs::host::ManagedServerStubReadyEntry* entry)
{
    auto* capture = static_cast<ManagedCallbackCapture*>(context);
    if (capture == nullptr || entry == nullptr)
    {
        XS_CHECK(false);
        return;
    }

    ++capture->ready.call_count;
    capture->ready.assignment_epochs.push_back(assignment_epoch);
    capture->ready.entity_types.push_back(ReadManagedUtf8(entry->entity_type_utf8, entry->entity_type_length));
    capture->ready.entity_ids.push_back(ReadManagedUtf8(entry->entity_id_utf8, entry->entity_id_length));
}

void OnManagedLog(void* context, std::uint32_t level, const std::uint8_t* category_utf8,
                  std::uint32_t category_length, const std::uint8_t* message_utf8, std::uint32_t message_length)
{
    auto* capture = static_cast<ManagedCallbackCapture*>(context);
    if (capture == nullptr)
    {
        XS_CHECK(false);
        return;
    }

    ++capture->logs.call_count;
    capture->logs.levels.push_back(level);
    capture->logs.categories.push_back(ReadManagedUtf8(category_utf8, category_length));
    capture->logs.messages.push_back(ReadManagedUtf8(message_utf8, message_length));
}

std::int64_t CreateOnceTimer(void* context, std::uint64_t delay_ms)
{
    auto* capture = static_cast<ManagedCallbackCapture*>(context);
    if (capture == nullptr || capture->timers.exports == nullptr || capture->timers.exports->on_native_timer == nullptr)
    {
        XS_CHECK(false);
        return static_cast<std::int64_t>(xs::core::TimerErrorCode::Unknown);
    }

    capture->timers.requested_create_once_delays_ms.push_back(delay_ms);

    const std::chrono::milliseconds actual_delay = delay_ms == 0U ? std::chrono::milliseconds::zero()
                                                                  : std::chrono::milliseconds(1);
    auto native_timer_id = std::make_shared<std::int64_t>(0);
    const xs::core::TimerCreateResult create_result =
        capture->timers.timer_manager.CreateOnce(actual_delay,
                                                 [capture, native_timer_id]()
                                                 {
                                                     capture->timers.fired_timer_ids.push_back(*native_timer_id);
                                                     if (capture->timers.exports == nullptr ||
                                                         capture->timers.exports->on_native_timer == nullptr)
                                                     {
                                                         XS_CHECK(false);
                                                         return;
                                                     }

                                                     XS_CHECK(capture->timers.exports->on_native_timer(*native_timer_id) == 0);
                                                 });
    if (!xs::core::IsTimerID(create_result))
    {
        XS_CHECK(false);
        return create_result;
    }

    *native_timer_id = static_cast<std::int64_t>(create_result);
    capture->timers.created_timer_ids.push_back(*native_timer_id);
    return *native_timer_id;
}

std::int32_t CancelTimer(void* context, std::int64_t timer_id)
{
    auto* capture = static_cast<ManagedCallbackCapture*>(context);
    if (capture == nullptr || timer_id <= 0)
    {
        XS_CHECK(false);
        return -1;
    }

    const xs::core::TimerErrorCode cancel_result =
        capture->timers.timer_manager.Cancel(static_cast<xs::core::TimerID>(timer_id));
    if (cancel_result != xs::core::TimerErrorCode::None)
    {
        XS_CHECK(false);
        return -1;
    }

    capture->timers.cancelled_timer_ids.push_back(timer_id);
    return 0;
}

std::int32_t ForwardMailboxCall(void* context, const std::uint8_t* target_game_node_id_utf8,
                                std::uint32_t target_game_node_id_length, const std::uint8_t* target_stub_type_utf8,
                                std::uint32_t target_stub_type_length, std::uint32_t msg_id, const std::uint8_t* payload,
                                std::uint32_t payload_length)
{
    auto* capture = static_cast<ManagedCallbackCapture*>(context);
    if (capture == nullptr ||
        (target_game_node_id_utf8 == nullptr && target_game_node_id_length != 0U) ||
        target_stub_type_utf8 == nullptr ||
        msg_id == 0U ||
        (payload == nullptr && payload_length != 0U))
    {
        XS_CHECK(false);
        return -1;
    }

    ++capture->forwarded_mailbox_calls.call_count;
    capture->forwarded_mailbox_calls.target_game_node_ids.push_back(
        ReadManagedUtf8(target_game_node_id_utf8, target_game_node_id_length));
    capture->forwarded_mailbox_calls.target_mailbox_names.push_back(
        ReadManagedUtf8(target_stub_type_utf8, target_stub_type_length));
    capture->forwarded_mailbox_calls.msg_ids.push_back(msg_id);
    if (payload == nullptr)
    {
        capture->forwarded_mailbox_calls.payloads.emplace_back();
    }
    else
    {
        const std::span<const std::byte> payload_view(
            reinterpret_cast<const std::byte*>(payload),
            static_cast<std::size_t>(payload_length));
        capture->forwarded_mailbox_calls.payloads.emplace_back(payload_view.begin(), payload_view.end());
    }

    return 0;
}

std::int32_t ForwardProxyCall(void* context, const std::uint8_t* route_gate_node_id_utf8,
                              std::uint32_t route_gate_node_id_length, const std::uint8_t* target_entity_id_utf8,
                              std::uint32_t target_entity_id_length, std::uint32_t msg_id, const std::uint8_t* payload,
                              std::uint32_t payload_length)
{
    auto* capture = static_cast<ManagedCallbackCapture*>(context);
    if (capture == nullptr || route_gate_node_id_utf8 == nullptr || target_entity_id_utf8 == nullptr || msg_id == 0U ||
        (payload == nullptr && payload_length != 0U))
    {
        XS_CHECK(false);
        return -1;
    }

    ++capture->forwarded_proxy_calls.call_count;
    capture->forwarded_proxy_calls.route_gate_node_ids.push_back(
        ReadManagedUtf8(route_gate_node_id_utf8, route_gate_node_id_length));
    capture->forwarded_proxy_calls.target_entity_ids.push_back(
        ReadManagedUtf8(target_entity_id_utf8, target_entity_id_length));
    capture->forwarded_proxy_calls.msg_ids.push_back(msg_id);
    if (payload == nullptr)
    {
        capture->forwarded_proxy_calls.payloads.emplace_back();
    }
    else
    {
        const std::span<const std::byte> payload_view(
            reinterpret_cast<const std::byte*>(payload),
            static_cast<std::size_t>(payload_length));
        capture->forwarded_proxy_calls.payloads.emplace_back(payload_view.begin(), payload_view.end());
    }

    return 0;
}

std::int32_t PushClientMessage(void* context, const std::uint8_t* route_gate_node_id_utf8,
                               std::uint32_t route_gate_node_id_length, const std::uint8_t* target_entity_id_utf8,
                               std::uint32_t target_entity_id_length, std::uint32_t msg_id, const std::uint8_t* payload,
                               std::uint32_t payload_length)
{
    auto* capture = static_cast<ManagedCallbackCapture*>(context);
    if (capture == nullptr || route_gate_node_id_utf8 == nullptr || target_entity_id_utf8 == nullptr || msg_id == 0U ||
        (payload == nullptr && payload_length != 0U))
    {
        XS_CHECK(false);
        return -1;
    }

    ++capture->pushed_client_messages.call_count;
    capture->pushed_client_messages.route_gate_node_ids.push_back(
        ReadManagedUtf8(route_gate_node_id_utf8, route_gate_node_id_length));
    capture->pushed_client_messages.target_entity_ids.push_back(
        ReadManagedUtf8(target_entity_id_utf8, target_entity_id_length));
    capture->pushed_client_messages.msg_ids.push_back(msg_id);
    if (payload == nullptr)
    {
        capture->pushed_client_messages.payloads.emplace_back();
    }
    else
    {
        const std::span<const std::byte> payload_view(
            reinterpret_cast<const std::byte*>(payload),
            static_cast<std::size_t>(payload_length));
        capture->pushed_client_messages.payloads.emplace_back(payload_view.begin(), payload_view.end());
    }

    return 0;
}

std::size_t CountManagedLogs(const ManagedLogCapture& capture, std::uint32_t level, std::string_view category,
                             std::string_view message)
{
    std::size_t count = 0U;
    for (std::size_t index = 0U; index < capture.levels.size() && index < capture.categories.size() &&
                                index < capture.messages.size();
         ++index)
    {
        if (capture.levels[index] == level && capture.categories[index] == category && capture.messages[index] == message)
        {
            ++count;
        }
    }

    return count;
}

bool ContainsText(const std::vector<std::string>& values, std::string_view expected)
{
    return std::find(values.begin(), values.end(), expected) != values.end();
}

bool ContainsLogMessageSubstring(const ManagedLogCapture& capture, std::string_view category, std::string_view substring)
{
    for (std::size_t index = 0U; index < capture.categories.size() && index < capture.messages.size(); ++index)
    {
        if (capture.categories[index] == category &&
            capture.messages[index].find(substring) != std::string::npos)
        {
            return true;
        }
    }

    return false;
}

std::vector<std::byte> EncodeRelayForwardMailboxCallPayload(std::string_view source_game_node_id,
                                                            std::string_view target_game_node_id,
                                                            std::string_view target_mailbox_name,
                                                            std::uint32_t msg_id,
                                                            std::span<const std::byte> payload,
                                                            std::string_view target_entity_id = {})
{
    const xs::net::RelayForwardMailboxCall relay_message{
        .source_game_node_id = std::string(source_game_node_id),
        .target_game_node_id = std::string(target_game_node_id),
        .target_entity_id = std::string(target_entity_id),
        .target_mailbox_name = std::string(target_mailbox_name),
        .mailbox_call_msg_id = msg_id,
        .relay_flags = 0u,
        .payload = std::vector<std::byte>(payload.begin(), payload.end()),
    };

    std::size_t wire_size = 0u;
    XS_CHECK(xs::net::GetRelayForwardMailboxCallWireSize(relay_message, &wire_size) == xs::net::RelayCodecErrorCode::None);
    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(xs::net::EncodeRelayForwardMailboxCall(relay_message, buffer) == xs::net::RelayCodecErrorCode::None);
    return buffer;
}

std::vector<std::byte> EncodeRelayForwardProxyCallPayload(std::string_view source_game_node_id,
                                                          std::string_view route_gate_node_id,
                                                          std::string_view target_entity_id, std::uint32_t msg_id,
                                                          std::span<const std::byte> payload)
{
    const xs::net::RelayForwardProxyCall relay_message{
        .source_game_node_id = std::string(source_game_node_id),
        .route_gate_node_id = std::string(route_gate_node_id),
        .target_entity_id = std::string(target_entity_id),
        .proxy_call_msg_id = msg_id,
        .relay_flags = 0u,
        .payload = std::vector<std::byte>(payload.begin(), payload.end()),
    };

    std::size_t wire_size = 0u;
    XS_CHECK(xs::net::GetRelayForwardProxyCallWireSize(relay_message, &wire_size) == xs::net::RelayCodecErrorCode::None);
    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(xs::net::EncodeRelayForwardProxyCall(relay_message, buffer) == xs::net::RelayCodecErrorCode::None);
    return buffer;
}

std::string BuildCreateAvatarPayload(std::string_view account_id, std::string_view avatar_id, std::string_view avatar_name,
                                     std::string_view gate_node_id, std::uint64_t session_id)
{
    return std::string("{\"accountId\":\"") + std::string(account_id) +
           "\",\"avatarId\":\"" + std::string(avatar_id) +
           "\",\"avatarName\":\"" + std::string(avatar_name) +
           "\",\"gateNodeId\":\"" + std::string(gate_node_id) +
           "\",\"sessionId\":" + std::to_string(session_id) + "}";
}

std::string BuildOnlineAvatarRegistrationPayload(std::string_view account_id, std::string_view entity_id,
                                                 std::uint64_t session_id, std::string_view gate_node_id,
                                                 std::string_view game_node_id, std::string_view display_name,
                                                 std::string_view proxy_route_gate_node_id)
{
    return std::string("{\"accountId\":\"") + std::string(account_id) +
           "\",\"entityId\":\"" + std::string(entity_id) +
           "\",\"sessionId\":" + std::to_string(session_id) +
           ",\"gateNodeId\":\"" + std::string(gate_node_id) +
           "\",\"gameNodeId\":\"" + std::string(game_node_id) +
           "\",\"displayName\":\"" + std::string(display_name) +
           "\",\"proxyEntityId\":\"" + std::string(entity_id) +
           "\",\"proxyRouteGateNodeId\":\"" + std::string(proxy_route_gate_node_id) +
           "\"}";
}

void PopulateManagedInitInput(ManagedInitInput* input, std::string_view node_id)
{
    if (input == nullptr)
    {
        XS_CHECK(false);
        return;
    }

    input->node_id = std::string(node_id);
    input->config_path = NormalizePath(kManagedRuntimeConfigPath).string();
    input->args = xs::host::ManagedInitArgs{
        .struct_size = sizeof(xs::host::ManagedInitArgs),
        .abi_version = xs::host::XS_MANAGED_ABI_VERSION,
        .process_type = 2U,
        .reserved0 = 0U,
        .node_id_utf8 = reinterpret_cast<const std::uint8_t*>(input->node_id.data()),
        .node_id_length = static_cast<std::uint32_t>(input->node_id.size()),
        .config_path_utf8 = reinterpret_cast<const std::uint8_t*>(input->config_path.data()),
        .config_path_length = static_cast<std::uint32_t>(input->config_path.size()),
    };
    input->args.native_callbacks.struct_size = sizeof(xs::host::ManagedNativeCallbacks);
    input->args.native_callbacks.reserved0 = 0U;
    input->args.native_callbacks.context = nullptr;
    input->args.native_callbacks.on_server_stub_ready = nullptr;
    input->args.native_callbacks.on_log = nullptr;
    input->args.native_callbacks.create_once_timer = nullptr;
    input->args.native_callbacks.cancel_timer = nullptr;
    input->args.native_callbacks.forward_stub_call = nullptr;
    input->args.native_callbacks.forward_proxy_call = nullptr;
    input->args.native_callbacks.push_client_message = nullptr;
}

xs::host::ManagedServerStubOwnershipEntry MakeOwnershipEntry(std::string_view entity_type, std::string_view entity_id,
                                                             std::string_view owner_game_node_id)
{
    xs::host::ManagedServerStubOwnershipEntry entry{};
    XS_CHECK(TryWriteManagedUtf8String(
        entity_type,
        std::span<std::uint8_t>(entry.entity_type_utf8, xs::host::XS_MANAGED_SERVER_STUB_ENTITY_TYPE_MAX_UTF8_BYTES),
        &entry.entity_type_length));
    XS_CHECK(TryWriteManagedUtf8String(
        entity_id,
        std::span<std::uint8_t>(entry.entity_id_utf8, xs::host::XS_MANAGED_SERVER_STUB_ENTITY_ID_MAX_UTF8_BYTES),
        &entry.entity_id_length));
    XS_CHECK(TryWriteManagedUtf8String(
        owner_game_node_id,
        std::span<std::uint8_t>(entry.owner_game_node_id_utf8, xs::host::XS_MANAGED_NODE_ID_MAX_UTF8_BYTES),
        &entry.owner_game_node_id_length));
    return entry;
}

void TestManagedAssetsExist()
{
    XS_CHECK_MSG(std::filesystem::exists(kManagedAssemblyPath), kManagedAssemblyPath.string().c_str());
    XS_CHECK_MSG(std::filesystem::exists(kManagedRuntimeConfigPath), kManagedRuntimeConfigPath.string().c_str());
    XS_CHECK_MSG(std::filesystem::exists(kManagedGameLogicAssemblyPath), kManagedGameLogicAssemblyPath.string().c_str());
    XS_CHECK_MSG(std::filesystem::exists(kAbiMismatchAssemblyPath), kAbiMismatchAssemblyPath.string().c_str());
    XS_CHECK_MSG(std::filesystem::exists(kAbiMismatchRuntimeConfigPath),
                 kAbiMismatchRuntimeConfigPath.string().c_str());
    XS_CHECK_MSG(std::filesystem::exists(kMissingExportsAssemblyPath), kMissingExportsAssemblyPath.string().c_str());
    XS_CHECK_MSG(std::filesystem::exists(kMissingExportsRuntimeConfigPath),
                 kMissingExportsRuntimeConfigPath.string().c_str());
}

void TestManagedHostErrorMetadata()
{
    XS_CHECK(xs::host::ManagedHostErrorCanonicalName(xs::host::ManagedHostErrorCode::RuntimeInitializeFailed) ==
             std::string_view("Interop.RuntimeInitializeFailed"));
    XS_CHECK(xs::host::ManagedHostErrorCanonicalName(xs::host::ManagedHostErrorCode::AbiVersionMismatch) ==
             std::string_view("Interop.AbiVersionMismatch"));
    XS_CHECK(xs::host::ManagedHostErrorMessage(xs::host::ManagedHostErrorCode::RuntimeDelegateLoadFailed) ==
             std::string_view("Failed to resolve load_assembly_and_get_function_pointer from hostfxr."));
    XS_CHECK(xs::host::ManagedHostErrorMessage(xs::host::ManagedHostErrorCode::RuntimeNotLoaded) ==
             std::string_view("Managed runtime host must be loaded before binding managed exports."));
    XS_CHECK(xs::host::ManagedHostErrorMessage(xs::host::ManagedHostErrorCode::EntryPointNotBound) ==
             std::string_view("Managed exports have not been bound successfully."));
}

void TestBindRejectsRuntimeNotLoaded()
{
    xs::host::ManagedRuntimeHost host;

    const xs::host::ManagedHostErrorCode result = host.BindExports();

    XS_CHECK(result == xs::host::ManagedHostErrorCode::RuntimeNotLoaded);
    XS_CHECK(xs::host::ManagedHostErrorMessage(result) ==
             std::string_view("Managed runtime host must be loaded before binding managed exports."));
    XS_CHECK(!host.IsLoaded());
    XS_CHECK(!host.AreExportsBound());
}

void TestGetExportsRejectsUnboundHost()
{
    xs::host::ManagedRuntimeHost host;
    xs::host::ManagedExports exports{};

    const xs::host::ManagedHostErrorCode result = host.GetExports(exports);

    XS_CHECK(result == xs::host::ManagedHostErrorCode::EntryPointNotBound);
    XS_CHECK(xs::host::ManagedHostErrorMessage(result) ==
             std::string_view("Managed exports have not been bound successfully."));
    XS_CHECK(!host.AreExportsBound());
}

void TestLoadRejectsMissingRuntimeConfig()
{
    xs::host::ManagedRuntimeHost host;
    const std::filesystem::path missing_runtime_config =
        std::filesystem::current_path() / "test-output" / "host-runtime-tests" / "missing.runtimeconfig.json";

    const xs::host::ManagedHostErrorCode result = host.Load(xs::host::ManagedRuntimeHostOptions{
        missing_runtime_config,
        kManagedAssemblyPath,
    });

    XS_CHECK(result == xs::host::ManagedHostErrorCode::RuntimeConfigPathNotFound);
    XS_CHECK(xs::host::ManagedHostErrorMessage(result) == std::string_view("Managed runtime config was not found."));
    XS_CHECK(!host.IsLoaded());
    XS_CHECK(!host.AreExportsBound());
}

void TestLoadRejectsMissingAssembly()
{
    xs::host::ManagedRuntimeHost host;
    const std::filesystem::path missing_assembly =
        std::filesystem::current_path() / "test-output" / "host-runtime-tests" / "missing-assembly.dll";

    const xs::host::ManagedHostErrorCode result = host.Load(xs::host::ManagedRuntimeHostOptions{
        kManagedRuntimeConfigPath,
        missing_assembly,
    });

    XS_CHECK(result == xs::host::ManagedHostErrorCode::AssemblyPathNotFound);
    XS_CHECK(xs::host::ManagedHostErrorMessage(result) == std::string_view("Managed root assembly was not found."));
    XS_CHECK(!host.IsLoaded());
    XS_CHECK(!host.AreExportsBound());
}

void TestLoadAndBindExportsSucceed()
{
    xs::host::ManagedRuntimeHost host;

    const xs::host::ManagedHostErrorCode load_result = host.Load(MakePrimaryManagedRuntimeHostOptions());

    XS_CHECK_MSG(load_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(load_result).c_str());
    XS_CHECK(host.IsLoaded());
    XS_CHECK(!host.AreExportsBound());
    XS_CHECK(host.load_assembly_and_get_function_pointer() != nullptr);
    XS_CHECK(NormalizePath(host.runtime_config_path()) == NormalizePath(kManagedRuntimeConfigPath));
    XS_CHECK(NormalizePath(host.assembly_path()) == NormalizePath(kManagedAssemblyPath));
    XS_CHECK(!host.hostfxr_path().empty());
    XS_CHECK(host.hostfxr_path().filename().string().find("hostfxr") != std::string::npos);

    xs::host::ManagedExports exports_before_bind{};
    const xs::host::ManagedHostErrorCode unbound_result = host.GetExports(exports_before_bind);
    XS_CHECK(unbound_result == xs::host::ManagedHostErrorCode::EntryPointNotBound);

    const xs::host::ManagedHostErrorCode bind_result = host.BindExports();
    XS_CHECK_MSG(bind_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(bind_result).c_str());
    XS_CHECK(host.AreExportsBound());

    const xs::host::ManagedHostErrorCode second_bind_result = host.BindExports();
    XS_CHECK(second_bind_result == xs::host::ManagedHostErrorCode::None);

    xs::host::ManagedExports exports{};
    const xs::host::ManagedHostErrorCode get_exports_result = host.GetExports(exports);
    XS_CHECK_MSG(get_exports_result == xs::host::ManagedHostErrorCode::None,
                 DescribeManagedHostResult(get_exports_result).c_str());
    XS_CHECK(exports.abi_version == xs::host::XS_MANAGED_ABI_VERSION);
    XS_CHECK(exports.get_abi_version != nullptr);
    XS_CHECK(exports.init != nullptr);
    XS_CHECK(exports.on_message != nullptr);
    XS_CHECK(exports.on_tick != nullptr);
    XS_CHECK(exports.on_native_timer != nullptr);
    XS_CHECK(exports.apply_server_stub_ownership != nullptr);
    XS_CHECK(exports.reset_server_stub_ownership != nullptr);
    XS_CHECK(exports.get_ready_server_stub_count != nullptr);
    XS_CHECK(exports.get_ready_server_stub_entry != nullptr);
    XS_CHECK(exports.get_server_stub_catalog_count != nullptr);
    XS_CHECK(exports.get_server_stub_catalog_entry != nullptr);
    XS_CHECK(exports.get_abi_version() == xs::host::XS_MANAGED_ABI_VERSION);

    ManagedInitInput init_input{};
    PopulateManagedInitInput(&init_input, "Game0");
    ManagedCallbackCapture callback_capture{};
    callback_capture.timers.exports = &exports;
    init_input.args.native_callbacks.context = &callback_capture;
    init_input.args.native_callbacks.on_server_stub_ready = &OnServerStubReady;
    init_input.args.native_callbacks.on_log = &OnManagedLog;
    init_input.args.native_callbacks.create_once_timer = &CreateOnceTimer;
    init_input.args.native_callbacks.cancel_timer = &CancelTimer;
    init_input.args.native_callbacks.forward_stub_call = &ForwardMailboxCall;
    init_input.args.native_callbacks.forward_proxy_call = &ForwardProxyCall;
    init_input.args.native_callbacks.push_client_message = &PushClientMessage;
    XS_CHECK(exports.init(&init_input.args) == 0);
    XS_CHECK(exports.on_message(nullptr) == 0);
    XS_CHECK(exports.on_tick(1234, 16) == 0);
    XS_CHECK(exports.reset_server_stub_ownership() == 0);

    std::uint32_t ready_count = 0U;
    XS_CHECK(exports.get_ready_server_stub_count(&ready_count) == 0);
    XS_CHECK(ready_count == 0U);

    std::vector<xs::host::ManagedServerStubOwnershipEntry> assignments;
    assignments.push_back(MakeOwnershipEntry("OnlineStub", "unknown", "Game0"));
    assignments.push_back(MakeOwnershipEntry("MatchStub", "unknown", "Game0"));
    assignments.push_back(MakeOwnershipEntry("ChatStub", "unknown", "Game9"));

    xs::host::ManagedServerStubOwnershipSync ownership_sync{};
    ownership_sync.struct_size = sizeof(xs::host::ManagedServerStubOwnershipSync);
    ownership_sync.assignment_epoch = 7U;
    ownership_sync.assignment_count = static_cast<std::uint32_t>(assignments.size());
    ownership_sync.assignments = assignments.data();

    XS_CHECK(exports.apply_server_stub_ownership(&ownership_sync) == 0);
    callback_capture.timers.RunUntilIdle();
    XS_CHECK(callback_capture.ready.call_count == 2U);
    XS_CHECK(callback_capture.ready.assignment_epochs.size() == 2U);
    XS_CHECK(callback_capture.ready.entity_types.size() == 2U);
    XS_CHECK(callback_capture.ready.entity_ids.size() == 2U);
    if (callback_capture.ready.assignment_epochs.size() == 2U)
    {
        XS_CHECK(callback_capture.ready.assignment_epochs[0] == 7U);
        XS_CHECK(callback_capture.ready.assignment_epochs[1] == 7U);
    }
    if (callback_capture.ready.entity_types.size() == 2U)
    {
        XS_CHECK(callback_capture.ready.entity_types[0] == "OnlineStub");
        XS_CHECK(callback_capture.ready.entity_types[1] == "MatchStub");
    }
    if (callback_capture.ready.entity_ids.size() == 2U)
    {
        XS_CHECK(IsCanonicalGuidText(callback_capture.ready.entity_ids[0]));
        XS_CHECK(IsCanonicalGuidText(callback_capture.ready.entity_ids[1]));
        XS_CHECK(callback_capture.ready.entity_ids[0] != callback_capture.ready.entity_ids[1]);
    }
    XS_CHECK(exports.get_ready_server_stub_count(&ready_count) == 0);
    XS_CHECK(ready_count == 2U);

    xs::host::ManagedServerStubReadyEntry first_ready_entry{};
    xs::host::ManagedServerStubReadyEntry second_ready_entry{};
    XS_CHECK(exports.get_ready_server_stub_entry(0U, &first_ready_entry) == 0);
    XS_CHECK(exports.get_ready_server_stub_entry(1U, &second_ready_entry) == 0);
    XS_CHECK(ReadManagedUtf8(first_ready_entry.entity_type_utf8, first_ready_entry.entity_type_length) ==
             "OnlineStub");
    XS_CHECK(ReadManagedUtf8(second_ready_entry.entity_type_utf8, second_ready_entry.entity_type_length) ==
             "MatchStub");
    XS_CHECK(
        IsCanonicalGuidText(ReadManagedUtf8(first_ready_entry.entity_id_utf8, first_ready_entry.entity_id_length)));
    XS_CHECK(
        IsCanonicalGuidText(ReadManagedUtf8(second_ready_entry.entity_id_utf8, second_ready_entry.entity_id_length)));
    XS_CHECK(ReadManagedUtf8(first_ready_entry.entity_id_utf8, first_ready_entry.entity_id_length) !=
             ReadManagedUtf8(second_ready_entry.entity_id_utf8, second_ready_entry.entity_id_length));
    XS_CHECK(first_ready_entry.ready == 1U);
    XS_CHECK(second_ready_entry.ready == 1U);
    if (callback_capture.ready.entity_ids.size() == 2U)
    {
        XS_CHECK(callback_capture.ready.entity_ids[0] ==
                 ReadManagedUtf8(first_ready_entry.entity_id_utf8, first_ready_entry.entity_id_length));
        XS_CHECK(callback_capture.ready.entity_ids[1] ==
                 ReadManagedUtf8(second_ready_entry.entity_id_utf8, second_ready_entry.entity_id_length));
    }

    XS_CHECK(exports.reset_server_stub_ownership() == 0);
    XS_CHECK(exports.get_ready_server_stub_count(&ready_count) == 0);
    XS_CHECK(ready_count == 0U);

    XS_CHECK(callback_capture.timers.requested_create_once_delays_ms.size() == 1U);
    XS_CHECK(callback_capture.timers.created_timer_ids.size() == 1U);
    XS_CHECK(callback_capture.timers.fired_timer_ids.size() == 1U);
    XS_CHECK(callback_capture.timers.cancelled_timer_ids.empty());
    XS_CHECK(callback_capture.forwarded_mailbox_calls.call_count == 0U);
    XS_CHECK(callback_capture.forwarded_proxy_calls.call_count == 0U);
    if (callback_capture.timers.requested_create_once_delays_ms.size() == 1U)
    {
        XS_CHECK(callback_capture.timers.requested_create_once_delays_ms[0] == 5000U);
    }
    XS_CHECK(callback_capture.logs.call_count >= 7U);
    XS_CHECK(callback_capture.logs.levels.size() == callback_capture.logs.call_count);
    XS_CHECK(callback_capture.logs.categories.size() == callback_capture.logs.call_count);
    XS_CHECK(callback_capture.logs.messages.size() == callback_capture.logs.call_count);
    XS_CHECK(CountManagedLogs(callback_capture.logs, static_cast<std::uint32_t>(xs::host::ManagedLogLevel::Info),
                              "managed.runtime", "Game managed runtime initialized.") == 1U);
    XS_CHECK(CountManagedLogs(callback_capture.logs, static_cast<std::uint32_t>(xs::host::ManagedLogLevel::Info),
                              "managed.runtime", "Game managed runtime applied server stub ownership.") == 1U);
    XS_CHECK(CountManagedLogs(callback_capture.logs, static_cast<std::uint32_t>(xs::host::ManagedLogLevel::Info),
                              "managed.runtime", "Game managed runtime reset server stub ownership state.") == 2U);
    XS_CHECK(CountManagedLogs(callback_capture.logs, static_cast<std::uint32_t>(xs::host::ManagedLogLevel::Debug),
                              "managed.runtime", "Game managed runtime published server stub ready.") == 2U);
    XS_CHECK(CountManagedLogs(callback_capture.logs, static_cast<std::uint32_t>(xs::host::ManagedLogLevel::Info),
                              "MatchStub", "MatchStub received call msgId=5101.") == 1U);

    XS_CHECK(host.Unload() == xs::host::ManagedHostErrorCode::None);
    XS_CHECK(!host.IsLoaded());
    XS_CHECK(!host.AreExportsBound());
    XS_CHECK(host.load_assembly_and_get_function_pointer() == nullptr);
    XS_CHECK(host.runtime_config_path().empty());
    XS_CHECK(host.assembly_path().empty());
    XS_CHECK(host.hostfxr_path().empty());

    xs::host::ManagedExports exports_after_unload{};
    const xs::host::ManagedHostErrorCode after_unload_result = host.GetExports(exports_after_unload);
    XS_CHECK(after_unload_result == xs::host::ManagedHostErrorCode::EntryPointNotBound);
}

void TestManagedExportsProvideServerStubCatalogFunctions()
{
    xs::host::ManagedRuntimeHost host;

    const xs::host::ManagedHostErrorCode load_result = host.Load(MakePrimaryManagedRuntimeHostOptions());
    XS_CHECK_MSG(load_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(load_result).c_str());
    XS_CHECK(host.IsLoaded());
    XS_CHECK(!host.AreExportsBound());

    xs::host::ManagedExports exports_before_bind{};
    const xs::host::ManagedHostErrorCode unbound_result = host.GetExports(exports_before_bind);
    XS_CHECK(unbound_result == xs::host::ManagedHostErrorCode::EntryPointNotBound);

    const xs::host::ManagedHostErrorCode bind_result = host.BindExports();
    XS_CHECK_MSG(bind_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(bind_result).c_str());
    XS_CHECK(host.AreExportsBound());

    const xs::host::ManagedHostErrorCode second_bind_result = host.BindExports();
    XS_CHECK(second_bind_result == xs::host::ManagedHostErrorCode::None);

    xs::host::ManagedExports exports{};
    const xs::host::ManagedHostErrorCode get_exports_result = host.GetExports(exports);
    XS_CHECK_MSG(get_exports_result == xs::host::ManagedHostErrorCode::None,
                 DescribeManagedHostResult(get_exports_result).c_str());
    XS_CHECK(exports.abi_version == xs::host::XS_MANAGED_ABI_VERSION);
    XS_CHECK(exports.get_server_stub_catalog_count != nullptr);
    XS_CHECK(exports.get_server_stub_catalog_entry != nullptr);

    std::uint32_t count = 0U;
    XS_CHECK(exports.get_server_stub_catalog_count(&count) == 0);
    XS_CHECK(count > 0U);

    std::vector<std::string> entity_types;
    entity_types.reserve(count);
    for (std::uint32_t index = 0U; index < count; ++index)
    {
        xs::host::ManagedServerStubCatalogEntry entry{};
        XS_CHECK(exports.get_server_stub_catalog_entry(index, &entry) == 0);
        entity_types.push_back(ReadManagedUtf8(entry.entity_type_utf8, entry.entity_type_length));
        XS_CHECK(ReadManagedUtf8(entry.entity_id_utf8, entry.entity_id_length) == "unknown");
    }

    XS_CHECK(ContainsText(entity_types, "OnlineStub"));
    XS_CHECK(ContainsText(entity_types, "ChatStub"));
    XS_CHECK(ContainsText(entity_types, "LeaderboardStub"));
    XS_CHECK(ContainsText(entity_types, "MatchStub"));
}

void TestManagedRuntimeForwardsRemoteMailboxCallThroughNativeCallback()
{
    xs::host::ManagedRuntimeHost host;

    const xs::host::ManagedHostErrorCode load_result = host.Load(MakePrimaryManagedRuntimeHostOptions());
    XS_CHECK_MSG(load_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(load_result).c_str());
    XS_CHECK_MSG(host.BindExports() == xs::host::ManagedHostErrorCode::None, "BindExports failed.");

    xs::host::ManagedExports exports{};
    XS_CHECK_MSG(host.GetExports(exports) == xs::host::ManagedHostErrorCode::None, "GetExports failed.");

    ManagedInitInput init_input{};
    PopulateManagedInitInput(&init_input, "Game0");
    ManagedCallbackCapture callback_capture{};
    callback_capture.timers.exports = &exports;
    init_input.args.native_callbacks.context = &callback_capture;
    init_input.args.native_callbacks.on_server_stub_ready = &OnServerStubReady;
    init_input.args.native_callbacks.on_log = &OnManagedLog;
    init_input.args.native_callbacks.create_once_timer = &CreateOnceTimer;
    init_input.args.native_callbacks.cancel_timer = &CancelTimer;
    init_input.args.native_callbacks.forward_stub_call = &ForwardMailboxCall;
    init_input.args.native_callbacks.forward_proxy_call = &ForwardProxyCall;
    init_input.args.native_callbacks.push_client_message = &PushClientMessage;

    XS_CHECK(exports.init(&init_input.args) == 0);

    std::vector<xs::host::ManagedServerStubOwnershipEntry> assignments;
    assignments.push_back(MakeOwnershipEntry("OnlineStub", "unknown", "Game0"));
    assignments.push_back(MakeOwnershipEntry("MatchStub", "unknown", "Game1"));

    xs::host::ManagedServerStubOwnershipSync ownership_sync{};
    ownership_sync.struct_size = sizeof(xs::host::ManagedServerStubOwnershipSync);
    ownership_sync.assignment_epoch = 11U;
    ownership_sync.assignment_count = static_cast<std::uint32_t>(assignments.size());
    ownership_sync.assignments = assignments.data();

    XS_CHECK(exports.apply_server_stub_ownership(&ownership_sync) == 0);
    callback_capture.timers.RunUntilIdle();

    XS_CHECK(callback_capture.forwarded_mailbox_calls.call_count == 1U);
    XS_CHECK(callback_capture.forwarded_mailbox_calls.target_game_node_ids.size() == 1U);
    XS_CHECK(callback_capture.forwarded_mailbox_calls.target_mailbox_names.size() == 1U);
    XS_CHECK(callback_capture.forwarded_mailbox_calls.msg_ids.size() == 1U);
    XS_CHECK(callback_capture.forwarded_mailbox_calls.payloads.size() == 1U);
    if (callback_capture.forwarded_mailbox_calls.target_game_node_ids.size() == 1U)
    {
        XS_CHECK(callback_capture.forwarded_mailbox_calls.target_game_node_ids[0].empty());
    }
    if (callback_capture.forwarded_mailbox_calls.target_mailbox_names.size() == 1U)
    {
        XS_CHECK(callback_capture.forwarded_mailbox_calls.target_mailbox_names[0] == "MatchStub");
    }
    if (callback_capture.forwarded_mailbox_calls.msg_ids.size() == 1U)
    {
        XS_CHECK(callback_capture.forwarded_mailbox_calls.msg_ids[0] == 5101U);
    }
    if (callback_capture.forwarded_mailbox_calls.payloads.size() == 1U)
    {
        const std::string payload_text(
            reinterpret_cast<const char*>(callback_capture.forwarded_mailbox_calls.payloads[0].data()),
            callback_capture.forwarded_mailbox_calls.payloads[0].size());
        XS_CHECK(payload_text == "online-startup-call");
    }
}

void TestManagedRuntimeForwardsOnlineBroadcastThroughNativeProxyCallback()
{
    xs::host::ManagedRuntimeHost host;

    const xs::host::ManagedHostErrorCode load_result = host.Load(MakePrimaryManagedRuntimeHostOptions());
    XS_CHECK_MSG(load_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(load_result).c_str());
    XS_CHECK_MSG(host.BindExports() == xs::host::ManagedHostErrorCode::None, "BindExports failed.");

    xs::host::ManagedExports exports{};
    XS_CHECK_MSG(host.GetExports(exports) == xs::host::ManagedHostErrorCode::None, "GetExports failed.");

    ManagedInitInput init_input{};
    PopulateManagedInitInput(&init_input, "Game0");
    ManagedCallbackCapture callback_capture{};
    callback_capture.timers.exports = &exports;
    init_input.args.native_callbacks.context = &callback_capture;
    init_input.args.native_callbacks.on_server_stub_ready = &OnServerStubReady;
    init_input.args.native_callbacks.on_log = &OnManagedLog;
    init_input.args.native_callbacks.create_once_timer = &CreateOnceTimer;
    init_input.args.native_callbacks.cancel_timer = &CancelTimer;
    init_input.args.native_callbacks.forward_stub_call = &ForwardMailboxCall;
    init_input.args.native_callbacks.forward_proxy_call = &ForwardProxyCall;
    init_input.args.native_callbacks.push_client_message = &PushClientMessage;

    XS_CHECK(exports.init(&init_input.args) == 0);

    std::vector<xs::host::ManagedServerStubOwnershipEntry> assignments;
    assignments.push_back(MakeOwnershipEntry("OnlineStub", "unknown", "Game0"));

    xs::host::ManagedServerStubOwnershipSync ownership_sync{};
    ownership_sync.struct_size = sizeof(xs::host::ManagedServerStubOwnershipSync);
    ownership_sync.assignment_epoch = 17U;
    ownership_sync.assignment_count = static_cast<std::uint32_t>(assignments.size());
    ownership_sync.assignments = assignments.data();

    XS_CHECK(exports.apply_server_stub_ownership(&ownership_sync) == 0);

    constexpr std::string_view kAvatarEntityId = "01234567-89ab-cdef-0123-456789abcdef";
    const std::string registration_payload_text = BuildOnlineAvatarRegistrationPayload(
        "account-remote",
        kAvatarEntityId,
        77U,
        "Gate7",
        "Game9",
        "Remote Hero",
        "Gate7");
    const std::span<const std::byte> registration_payload(
        reinterpret_cast<const std::byte*>(registration_payload_text.data()),
        registration_payload_text.size());
    const std::vector<std::byte> register_relay_payload =
        EncodeRelayForwardMailboxCallPayload("GM", "Game0", "OnlineStub", 5200U, registration_payload);

    const xs::host::ManagedMessageView register_message{
        .struct_size = sizeof(xs::host::ManagedMessageView),
        .msg_id = xs::net::kRelayForwardMailboxCallMsgId,
        .seq = 0U,
        .flags = 0U,
        .session_id = 0U,
        .player_id = 0U,
        .payload = reinterpret_cast<const std::uint8_t*>(register_relay_payload.data()),
        .payload_length = static_cast<std::uint32_t>(register_relay_payload.size()),
        .reserved0 = 0U,
    };
    XS_CHECK(exports.on_message(&register_message) == 0);

    const std::string broadcast_payload_text = "hello-online";
    const std::span<const std::byte> broadcast_payload(
        reinterpret_cast<const std::byte*>(broadcast_payload_text.data()),
        broadcast_payload_text.size());
    const std::vector<std::byte> broadcast_relay_payload =
        EncodeRelayForwardMailboxCallPayload("GM", "Game0", "OnlineStub", 5201U, broadcast_payload);

    const xs::host::ManagedMessageView broadcast_message{
        .struct_size = sizeof(xs::host::ManagedMessageView),
        .msg_id = xs::net::kRelayForwardMailboxCallMsgId,
        .seq = 0U,
        .flags = 0U,
        .session_id = 0U,
        .player_id = 0U,
        .payload = reinterpret_cast<const std::uint8_t*>(broadcast_relay_payload.data()),
        .payload_length = static_cast<std::uint32_t>(broadcast_relay_payload.size()),
        .reserved0 = 0U,
    };
    XS_CHECK(exports.on_message(&broadcast_message) == 0);

    XS_CHECK(callback_capture.forwarded_proxy_calls.call_count == 1U);
    XS_CHECK(callback_capture.forwarded_proxy_calls.route_gate_node_ids.size() == 1U);
    XS_CHECK(callback_capture.forwarded_proxy_calls.target_entity_ids.size() == 1U);
    XS_CHECK(callback_capture.forwarded_proxy_calls.msg_ids.size() == 1U);
    XS_CHECK(callback_capture.forwarded_proxy_calls.payloads.size() == 1U);
    if (callback_capture.forwarded_proxy_calls.route_gate_node_ids.size() == 1U)
    {
        XS_CHECK(callback_capture.forwarded_proxy_calls.route_gate_node_ids[0] == "Gate7");
    }
    if (callback_capture.forwarded_proxy_calls.target_entity_ids.size() == 1U)
    {
        XS_CHECK(callback_capture.forwarded_proxy_calls.target_entity_ids[0] == kAvatarEntityId);
    }
    if (callback_capture.forwarded_proxy_calls.msg_ids.size() == 1U)
    {
        XS_CHECK(callback_capture.forwarded_proxy_calls.msg_ids[0] == 6201U);
    }
    if (callback_capture.forwarded_proxy_calls.payloads.size() == 1U)
    {
        const std::string payload_text(
            reinterpret_cast<const char*>(callback_capture.forwarded_proxy_calls.payloads[0].data()),
            callback_capture.forwarded_proxy_calls.payloads[0].size());
        XS_CHECK(payload_text == "hello-online");
    }
}

void TestManagedRuntimeDispatchesForwardedMailboxCallMessageToTargetStub()
{
    xs::host::ManagedRuntimeHost host;

    const xs::host::ManagedHostErrorCode load_result = host.Load(MakePrimaryManagedRuntimeHostOptions());
    XS_CHECK_MSG(load_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(load_result).c_str());
    XS_CHECK_MSG(host.BindExports() == xs::host::ManagedHostErrorCode::None, "BindExports failed.");

    xs::host::ManagedExports exports{};
    XS_CHECK_MSG(host.GetExports(exports) == xs::host::ManagedHostErrorCode::None, "GetExports failed.");

    ManagedInitInput init_input{};
    PopulateManagedInitInput(&init_input, "Game1");
    ManagedCallbackCapture callback_capture{};
    callback_capture.timers.exports = &exports;
    init_input.args.native_callbacks.context = &callback_capture;
    init_input.args.native_callbacks.on_server_stub_ready = &OnServerStubReady;
    init_input.args.native_callbacks.on_log = &OnManagedLog;
    init_input.args.native_callbacks.create_once_timer = &CreateOnceTimer;
    init_input.args.native_callbacks.cancel_timer = &CancelTimer;
    init_input.args.native_callbacks.forward_stub_call = &ForwardMailboxCall;
    init_input.args.native_callbacks.forward_proxy_call = &ForwardProxyCall;
    init_input.args.native_callbacks.push_client_message = &PushClientMessage;

    XS_CHECK(exports.init(&init_input.args) == 0);

    std::vector<xs::host::ManagedServerStubOwnershipEntry> assignments;
    assignments.push_back(MakeOwnershipEntry("MatchStub", "unknown", "Game1"));

    xs::host::ManagedServerStubOwnershipSync ownership_sync{};
    ownership_sync.struct_size = sizeof(xs::host::ManagedServerStubOwnershipSync);
    ownership_sync.assignment_epoch = 13U;
    ownership_sync.assignment_count = static_cast<std::uint32_t>(assignments.size());
    ownership_sync.assignments = assignments.data();

    XS_CHECK(exports.apply_server_stub_ownership(&ownership_sync) == 0);

    const std::string stub_payload_text = "cross-game-call";
    const std::span<const std::byte> stub_payload(
        reinterpret_cast<const std::byte*>(stub_payload_text.data()),
        stub_payload_text.size());
    const std::vector<std::byte> relay_payload =
        EncodeRelayForwardMailboxCallPayload("Game0", "Game1", "MatchStub", 5101U, stub_payload);

    const xs::host::ManagedMessageView message{
        .struct_size = sizeof(xs::host::ManagedMessageView),
        .msg_id = xs::net::kRelayForwardMailboxCallMsgId,
        .seq = 0U,
        .flags = 0U,
        .session_id = 0U,
        .player_id = 0U,
        .payload = reinterpret_cast<const std::uint8_t*>(relay_payload.data()),
        .payload_length = static_cast<std::uint32_t>(relay_payload.size()),
        .reserved0 = 0U,
    };

    XS_CHECK(exports.on_message(&message) == 0);
    XS_CHECK(CountManagedLogs(callback_capture.logs, static_cast<std::uint32_t>(xs::host::ManagedLogLevel::Info),
                              "MatchStub", "MatchStub received call msgId=5101.") == 1U);
}

void TestManagedRuntimeDispatchesForwardedProxyCallMessageToAvatar()
{
    xs::host::ManagedRuntimeHost host;

    const xs::host::ManagedHostErrorCode load_result = host.Load(MakePrimaryManagedRuntimeHostOptions());
    XS_CHECK_MSG(load_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(load_result).c_str());
    XS_CHECK_MSG(host.BindExports() == xs::host::ManagedHostErrorCode::None, "BindExports failed.");

    xs::host::ManagedExports exports{};
    XS_CHECK_MSG(host.GetExports(exports) == xs::host::ManagedHostErrorCode::None, "GetExports failed.");

    ManagedInitInput init_input{};
    PopulateManagedInitInput(&init_input, "Game1");
    ManagedCallbackCapture callback_capture{};
    callback_capture.timers.exports = &exports;
    init_input.args.native_callbacks.context = &callback_capture;
    init_input.args.native_callbacks.on_server_stub_ready = &OnServerStubReady;
    init_input.args.native_callbacks.on_log = &OnManagedLog;
    init_input.args.native_callbacks.create_once_timer = &CreateOnceTimer;
    init_input.args.native_callbacks.cancel_timer = &CancelTimer;
    init_input.args.native_callbacks.forward_stub_call = &ForwardMailboxCall;
    init_input.args.native_callbacks.forward_proxy_call = &ForwardProxyCall;
    init_input.args.native_callbacks.push_client_message = &PushClientMessage;

    XS_CHECK(exports.init(&init_input.args) == 0);

    std::vector<xs::host::ManagedServerStubOwnershipEntry> assignments;
    assignments.push_back(MakeOwnershipEntry("OnlineStub", "unknown", "Game1"));

    xs::host::ManagedServerStubOwnershipSync ownership_sync{};
    ownership_sync.struct_size = sizeof(xs::host::ManagedServerStubOwnershipSync);
    ownership_sync.assignment_epoch = 19U;
    ownership_sync.assignment_count = static_cast<std::uint32_t>(assignments.size());
    ownership_sync.assignments = assignments.data();

    XS_CHECK(exports.apply_server_stub_ownership(&ownership_sync) == 0);

    constexpr std::string_view kAvatarEntityId = "89abcdef-0123-4567-89ab-cdef01234567";
    const std::string create_payload_text = BuildCreateAvatarPayload(
        "proxy-account",
        kAvatarEntityId,
        "Proxy Hero",
        "Gate3",
        84U);
    const xs::host::ManagedMessageView create_message{
        .struct_size = sizeof(xs::host::ManagedMessageView),
        .msg_id = 2003U,
        .seq = 0U,
        .flags = 0U,
        .session_id = 0U,
        .player_id = 0U,
        .payload = reinterpret_cast<const std::uint8_t*>(create_payload_text.data()),
        .payload_length = static_cast<std::uint32_t>(create_payload_text.size()),
        .reserved0 = 0U,
    };
    XS_CHECK(exports.on_message(&create_message) == 0);

    const std::string forwarded_payload_text = "proxy-delivery";
    const std::span<const std::byte> forwarded_payload(
        reinterpret_cast<const std::byte*>(forwarded_payload_text.data()),
        forwarded_payload_text.size());
    const std::vector<std::byte> relay_payload =
        EncodeRelayForwardProxyCallPayload("Game0", "Gate3", kAvatarEntityId, 6201U, forwarded_payload);

    const xs::host::ManagedMessageView message{
        .struct_size = sizeof(xs::host::ManagedMessageView),
        .msg_id = xs::net::kRelayForwardProxyCallMsgId,
        .seq = 0U,
        .flags = 0U,
        .session_id = 0U,
        .player_id = 0U,
        .payload = reinterpret_cast<const std::uint8_t*>(relay_payload.data()),
        .payload_length = static_cast<std::uint32_t>(relay_payload.size()),
        .reserved0 = 0U,
    };

    XS_CHECK(exports.on_message(&message) == 0);
    XS_CHECK(callback_capture.forwarded_proxy_calls.call_count == 0U);
    XS_CHECK(callback_capture.pushed_client_messages.call_count == 1U);
    XS_CHECK(ContainsLogMessageSubstring(
        callback_capture.logs,
        "AvatarEntity",
        "received proxy call msgId=6201."));
    XS_CHECK(callback_capture.pushed_client_messages.route_gate_node_ids.size() == 1U);
    XS_CHECK(callback_capture.pushed_client_messages.target_entity_ids.size() == 1U);
    XS_CHECK(callback_capture.pushed_client_messages.msg_ids.size() == 1U);
    XS_CHECK(callback_capture.pushed_client_messages.payloads.size() == 1U);
    if (callback_capture.pushed_client_messages.route_gate_node_ids.size() == 1U)
    {
        XS_CHECK(callback_capture.pushed_client_messages.route_gate_node_ids[0] == "Gate3");
    }
    if (callback_capture.pushed_client_messages.target_entity_ids.size() == 1U)
    {
        XS_CHECK(callback_capture.pushed_client_messages.target_entity_ids[0] == kAvatarEntityId);
    }
    if (callback_capture.pushed_client_messages.msg_ids.size() == 1U)
    {
        XS_CHECK(callback_capture.pushed_client_messages.msg_ids[0] == 6201U);
    }
    if (callback_capture.pushed_client_messages.payloads.size() == 1U)
    {
        const std::string payload_text(
            reinterpret_cast<const char*>(callback_capture.pushed_client_messages.payloads[0].data()),
            callback_capture.pushed_client_messages.payloads[0].size());
        XS_CHECK(payload_text == "proxy-delivery");
    }
}

void TestLoadAllowsSecondInitializationAfterUnload()
{
    xs::host::ManagedRuntimeHost first_host;

    const xs::host::ManagedHostErrorCode first_load_result = first_host.Load(MakePrimaryManagedRuntimeHostOptions());
    XS_CHECK_MSG(first_load_result == xs::host::ManagedHostErrorCode::None,
                 DescribeManagedHostResult(first_load_result).c_str());

    const xs::host::ManagedHostErrorCode first_bind_result = first_host.BindExports();
    XS_CHECK_MSG(first_bind_result == xs::host::ManagedHostErrorCode::None,
                 DescribeManagedHostResult(first_bind_result).c_str());

    XS_CHECK(first_host.Unload() == xs::host::ManagedHostErrorCode::None);
    XS_CHECK(!first_host.IsLoaded());
    XS_CHECK(!first_host.AreExportsBound());

    xs::host::ManagedRuntimeHost second_host;

    const xs::host::ManagedHostErrorCode second_load_result = second_host.Load(xs::host::ManagedRuntimeHostOptions{
        kAbiMismatchRuntimeConfigPath,
        kAbiMismatchAssemblyPath,
    });
    XS_CHECK_MSG(second_load_result == xs::host::ManagedHostErrorCode::None,
                 DescribeManagedHostResult(second_load_result).c_str());
    XS_CHECK(second_host.IsLoaded());

    const xs::host::ManagedHostErrorCode second_bind_result = second_host.BindExports();
    XS_CHECK(second_bind_result == xs::host::ManagedHostErrorCode::AbiVersionMismatch);
    XS_CHECK(xs::host::ManagedHostErrorMessage(second_bind_result) ==
             std::string_view("Managed ABI version did not match the native host expectation."));
    XS_CHECK(!second_host.AreExportsBound());
}

void TestBindRejectsAbiMismatch()
{
    xs::host::ManagedRuntimeHost host;

    const xs::host::ManagedHostErrorCode load_result = host.Load(xs::host::ManagedRuntimeHostOptions{
        kAbiMismatchRuntimeConfigPath,
        kAbiMismatchAssemblyPath,
    });
    XS_CHECK_MSG(load_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(load_result).c_str());

    const xs::host::ManagedHostErrorCode bind_result = host.BindExports();
    XS_CHECK(bind_result == xs::host::ManagedHostErrorCode::AbiVersionMismatch);
    XS_CHECK(xs::host::ManagedHostErrorMessage(bind_result) ==
             std::string_view("Managed ABI version did not match the native host expectation."));
    XS_CHECK(!host.AreExportsBound());

    xs::host::ManagedExports exports{};
    const xs::host::ManagedHostErrorCode get_exports_result = host.GetExports(exports);
    XS_CHECK(get_exports_result == xs::host::ManagedHostErrorCode::EntryPointNotBound);
}

void TestBindRejectsMissingExport()
{
    xs::host::ManagedRuntimeHost host;

    const xs::host::ManagedHostErrorCode load_result = host.Load(xs::host::ManagedRuntimeHostOptions{
        kMissingExportsRuntimeConfigPath,
        kMissingExportsAssemblyPath,
    });
    XS_CHECK_MSG(load_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(load_result).c_str());

    const xs::host::ManagedHostErrorCode bind_result = host.BindExports();
    XS_CHECK(bind_result == xs::host::ManagedHostErrorCode::EntryPointResolveFailed);
    XS_CHECK(xs::host::ManagedHostErrorMessage(bind_result) ==
             std::string_view("Failed to resolve a required managed entry point from the managed runtime assembly."));
    XS_CHECK(!host.AreExportsBound());
}

} // namespace

int main()
{
    TestManagedAssetsExist();
    TestManagedHostErrorMetadata();
    TestBindRejectsRuntimeNotLoaded();
    TestGetExportsRejectsUnboundHost();
    TestLoadRejectsMissingRuntimeConfig();
    TestLoadRejectsMissingAssembly();
    TestLoadAndBindExportsSucceed();
    TestManagedRuntimeForwardsRemoteMailboxCallThroughNativeCallback();
    TestManagedRuntimeForwardsOnlineBroadcastThroughNativeProxyCallback();
    TestManagedRuntimeDispatchesForwardedMailboxCallMessageToTargetStub();
    TestManagedRuntimeDispatchesForwardedProxyCallMessageToAvatar();
    TestManagedExportsProvideServerStubCatalogFunctions();
    TestLoadAllowsSecondInitializationAfterUnload();
    TestBindRejectsAbiMismatch();
    TestBindRejectsMissingExport();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " managed host runtime test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

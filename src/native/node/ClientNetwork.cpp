#include "ClientNetwork.h"

#include "ClientSession.h"
#include "TimeUtils.h"

#include <asio/ip/address.hpp>
#include <asio/ip/udp.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace xs::node
{
namespace
{

constexpr std::size_t kMaxUdpDatagramBytes = 65535U;
constexpr std::size_t kKcpConversationHeaderBytes = 4U;

struct TransportKey final
{
    std::string remote_host{};
    std::uint16_t remote_port{0U};
    std::uint32_t conversation{0U};

    [[nodiscard]] bool operator<(const TransportKey& other) const noexcept
    {
        return std::tie(remote_host, remote_port, conversation) <
               std::tie(other.remote_host, other.remote_port, other.conversation);
    }
};

void ClearError(std::string& error_message) noexcept
{
    error_message.clear();
}

NodeErrorCode SetError(
    std::string& error_message,
    NodeErrorCode code,
    std::string message)
{
    if (message.empty())
    {
        error_message = std::string(NodeErrorMessage(code));
    }
    else
    {
        error_message = std::move(message);
    }

    return code;
}

std::string NormalizeHost(std::string_view host)
{
    if (host.size() >= 2U && host.front() == '[' && host.back() == ']')
    {
        return std::string(host.substr(1U, host.size() - 2U));
    }

    return std::string(host);
}

std::string ToEndpointText(std::string_view listen_endpoint)
{
    return std::string(listen_endpoint);
}

std::string ToEndpointText(const xs::net::Endpoint& endpoint)
{
    return endpoint.host + ':' + std::to_string(endpoint.port);
}

xs::net::Endpoint ToNetEndpoint(const asio::ip::udp::endpoint& endpoint)
{
    return xs::net::Endpoint{
        .host = endpoint.address().to_string(),
        .port = endpoint.port(),
    };
}

TransportKey MakeTransportKey(const xs::net::Endpoint& endpoint, std::uint32_t conversation)
{
    return TransportKey{
        .remote_host = NormalizeHost(endpoint.host),
        .remote_port = endpoint.port,
        .conversation = conversation,
    };
}

std::optional<xs::net::Endpoint> ParseConfiguredEndpoint(
    std::string_view text,
    std::string* error_message)
{
    if (text.empty())
    {
        if (error_message != nullptr)
        {
            *error_message = "ClientNetwork listen_endpoint must not be empty.";
        }
        return std::nullopt;
    }

    const std::size_t separator = text.rfind(':');
    if (separator == std::string_view::npos || separator == 0U || separator + 1U >= text.size())
    {
        if (error_message != nullptr)
        {
            *error_message = "ClientNetwork listen_endpoint must be formatted as <host>:<port>.";
        }
        return std::nullopt;
    }

    const std::string host = NormalizeHost(text.substr(0U, separator));
    if (host.empty())
    {
        if (error_message != nullptr)
        {
            *error_message = "ClientNetwork listen_endpoint host must not be empty.";
        }
        return std::nullopt;
    }

    std::uint32_t parsed_port = 0U;
    try
    {
        parsed_port = static_cast<std::uint32_t>(std::stoul(std::string(text.substr(separator + 1U))));
    }
    catch (const std::exception&)
    {
        if (error_message != nullptr)
        {
            *error_message = "ClientNetwork listen_endpoint port must be a valid unsigned integer.";
        }
        return std::nullopt;
    }

    if (parsed_port == 0U || parsed_port > 65535U)
    {
        if (error_message != nullptr)
        {
            *error_message = "ClientNetwork listen_endpoint port must be in the range 1-65535.";
        }
        return std::nullopt;
    }

    return xs::net::Endpoint{
        .host = host,
        .port = static_cast<std::uint16_t>(parsed_port),
    };
}

bool TryMakeUdpEndpoint(
    const xs::net::Endpoint& endpoint,
    asio::ip::udp::endpoint* udp_endpoint,
    std::string* error_message)
{
    if (udp_endpoint == nullptr)
    {
        if (error_message != nullptr)
        {
            *error_message = "UDP endpoint output must not be null.";
        }
        return false;
    }

    const std::string normalized_host = NormalizeHost(endpoint.host);
    if (normalized_host.empty())
    {
        if (error_message != nullptr)
        {
            *error_message = "Endpoint host must not be empty.";
        }
        return false;
    }

    if (endpoint.port == 0U)
    {
        if (error_message != nullptr)
        {
            *error_message = "Endpoint port must be greater than zero.";
        }
        return false;
    }

    std::error_code error_code;
    const asio::ip::address address = asio::ip::make_address(normalized_host, error_code);
    if (error_code)
    {
        if (error_message != nullptr)
        {
            *error_message = "Failed to parse IP address '" + normalized_host + "': " + error_code.message();
        }
        return false;
    }

    *udp_endpoint = asio::ip::udp::endpoint(address, endpoint.port);
    return true;
}

bool TryReadKcpConversation(
    std::span<const std::byte> datagram,
    std::uint32_t* conversation) noexcept
{
    if (conversation == nullptr || datagram.size() < kKcpConversationHeaderBytes)
    {
        return false;
    }

    *conversation =
        static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(datagram[0])) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(datagram[1])) << 8U) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(datagram[2])) << 16U) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(datagram[3])) << 24U);
    return true;
}

std::uint64_t CurrentUnixTimeMilliseconds() noexcept
{
    const std::int64_t now_unix_ms = xs::core::ToUnixTimeMilliseconds(xs::core::UtcNow());
    return now_unix_ms > 0 ? static_cast<std::uint64_t>(now_unix_ms) : 0U;
}

std::uint32_t CurrentKcpClockMilliseconds() noexcept
{
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        xs::core::SteadyNow().time_since_epoch());
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(now.count()));
}

} // namespace

class ClientNetwork::Impl final
{
  public:
    Impl(xs::core::MainEventLoop& event_loop, xs::core::Logger& logger, ClientNetworkOptions options)
        : event_loop_(event_loop), logger_(logger), options_(std::move(options))
    {
    }

    [[nodiscard]] NodeErrorCode Init()
    {
        if (initialized_)
        {
            return SetError(last_error_message_, NodeErrorCode::InvalidArgument, "ClientNetwork is already initialized.");
        }

        if (options_.listen_endpoint.empty())
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "ClientNetwork listen_endpoint must not be empty.");
        }

        if (options_.owner_node_id.empty())
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "ClientNetwork owner_node_id must not be empty.");
        }

        std::string endpoint_error;
        const std::optional<xs::net::Endpoint> parsed_endpoint =
            ParseConfiguredEndpoint(options_.listen_endpoint, &endpoint_error);
        if (!parsed_endpoint.has_value())
        {
            return SetError(last_error_message_, NodeErrorCode::InvalidArgument, std::move(endpoint_error));
        }

        const xs::net::KcpPeer probe({
            .conversation = 0U,
            .config = options_.kcp,
        });
        if (!probe.valid())
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::NodeInitFailed,
                "ClientNetwork KCP configuration is invalid: " + std::string(probe.last_error_message()));
        }

        listen_endpoint_ = *parsed_endpoint;

        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
            xs::core::LogContextField{"listenEndpoint", ToEndpointText(options_.listen_endpoint)},
            xs::core::LogContextField{"kcpMtu", std::to_string(options_.kcp.mtu)},
            xs::core::LogContextField{"kcpIntervalMs", std::to_string(options_.kcp.interval_ms)},
            xs::core::LogContextField{"sessionCount", "0"},
        };
        logger_.Log(xs::core::LogLevel::Info, "client.network", "Client network initialized.", context);

        initialized_ = true;
        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }
    [[nodiscard]] NodeErrorCode Run()
    {
        if (!initialized_)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "ClientNetwork must be initialized before Run().");
        }

        if (running_)
        {
            ClearError(last_error_message_);
            return NodeErrorCode::None;
        }

        asio::ip::udp::endpoint udp_endpoint;
        std::string endpoint_error;
        if (!TryMakeUdpEndpoint(listen_endpoint_, &udp_endpoint, &endpoint_error))
        {
            return SetError(last_error_message_, NodeErrorCode::NodeRunFailed, std::move(endpoint_error));
        }

        socket_ = std::make_unique<asio::ip::udp::socket>(event_loop_.context());

        std::error_code error_code;
        socket_->open(udp_endpoint.protocol(), error_code);
        if (error_code)
        {
            socket_.reset();
            return SetError(
                last_error_message_,
                NodeErrorCode::NodeRunFailed,
                "ClientNetwork failed to open UDP socket: " + error_code.message());
        }

        socket_->set_option(asio::socket_base::reuse_address(true), error_code);
        if (error_code)
        {
            socket_->close();
            socket_.reset();
            return SetError(
                last_error_message_,
                NodeErrorCode::NodeRunFailed,
                "ClientNetwork failed to configure UDP socket: " + error_code.message());
        }

        socket_->bind(udp_endpoint, error_code);
        if (error_code)
        {
            socket_->close();
            socket_.reset();
            return SetError(
                last_error_message_,
                NodeErrorCode::NodeRunFailed,
                "ClientNetwork failed to bind UDP listener: " + error_code.message());
        }

        running_ = true;

        const NodeErrorCode timer_result = StartKcpUpdateTimer();
        if (timer_result != NodeErrorCode::None)
        {
            running_ = false;
            std::error_code close_error;
            socket_->close(close_error);
            socket_.reset();
            return timer_result;
        }

        StartReceive();

        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
            xs::core::LogContextField{"listenEndpoint", ToEndpointText(options_.listen_endpoint)},
            xs::core::LogContextField{"sessionCount", std::to_string(sessions_.size())},
        };
        logger_.Log(xs::core::LogLevel::Info, "client.network", "Client network started.", context);

        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] NodeErrorCode Stop()
    {
        if (!initialized_ || !running_)
        {
            ClearError(last_error_message_);
            return NodeErrorCode::None;
        }

        running_ = false;
        CancelKcpUpdateTimer();

        std::error_code error_code;
        if (socket_ != nullptr)
        {
            socket_->close(error_code);
            socket_.reset();
        }

        if (error_code)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::NodeRunFailed,
                "ClientNetwork failed to close UDP listener: " + error_code.message());
        }

        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
            xs::core::LogContextField{"listenEndpoint", ToEndpointText(options_.listen_endpoint)},
            xs::core::LogContextField{"sessionCount", std::to_string(sessions_.size())},
        };
        logger_.Log(xs::core::LogLevel::Info, "client.network", "Client network stopped.", context);

        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] NodeErrorCode Uninit() noexcept
    {
        const NodeErrorCode stop_result = Stop();

        sessions_.clear();
        transport_index_.clear();
        next_session_id_ = 1U;
        listen_endpoint_ = xs::net::Endpoint{};
        socket_.reset();
        CancelKcpUpdateTimer();
        running_ = false;
        initialized_ = false;

        if (stop_result != NodeErrorCode::None)
        {
            return stop_result;
        }

        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] NodeErrorCode CreateSession(
        std::uint32_t conversation,
        const xs::net::Endpoint& remote_endpoint,
        std::uint64_t* session_id,
        std::uint64_t connected_at_unix_ms)
    {
        if (!initialized_)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "ClientNetwork must be initialized before CreateSession().");
        }

        const std::string normalized_host = NormalizeHost(remote_endpoint.host);
        if (normalized_host.empty())
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "ClientNetwork session remote endpoint host must not be empty.");
        }

        if (remote_endpoint.port == 0U)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "ClientNetwork session remote endpoint port must be greater than zero.");
        }

        const xs::net::Endpoint normalized_endpoint{
            .host = normalized_host,
            .port = remote_endpoint.port,
        };
        const TransportKey transport_key = MakeTransportKey(normalized_endpoint, conversation);
        if (transport_index_.contains(transport_key))
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "ClientNetwork session transport key is already registered.");
        }

        const std::uint64_t allocated_session_id = ConsumeNextSessionId();
        const std::uint64_t connected_at =
            connected_at_unix_ms != 0U
                ? connected_at_unix_ms
                : CurrentUnixTimeMilliseconds();

        auto session = std::make_unique<ClientSession>(
            ClientSessionOptions{
                .gate_node_id = options_.owner_node_id,
                .session_id = allocated_session_id,
                .conversation = conversation,
                .remote_endpoint = normalized_endpoint,
                .kcp = options_.kcp,
                .connected_at_unix_ms = connected_at,
            });
        if (!session->valid())
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::NodeInitFailed,
                "ClientNetwork failed to create session: " + std::string(session->last_error_message()));
        }

        transport_index_.emplace(transport_key, allocated_session_id);
        sessions_.emplace(allocated_session_id, std::move(session));
        if (session_id != nullptr)
        {
            *session_id = allocated_session_id;
        }

        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
            xs::core::LogContextField{"sessionId", std::to_string(allocated_session_id)},
            xs::core::LogContextField{"conversation", std::to_string(conversation)},
            xs::core::LogContextField{"remoteEndpoint", ToEndpointText(normalized_endpoint)},
            xs::core::LogContextField{"sessionCount", std::to_string(sessions_.size())},
        };
        logger_.Log(xs::core::LogLevel::Info, "client.kcp", "Client session created.", context);

        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] ClientSession* FindSession(std::uint64_t session_id) noexcept
    {
        const auto iterator = sessions_.find(session_id);
        return iterator != sessions_.end() ? iterator->second.get() : nullptr;
    }

    [[nodiscard]] const ClientSession* FindSession(std::uint64_t session_id) const noexcept
    {
        const auto iterator = sessions_.find(session_id);
        return iterator != sessions_.end() ? iterator->second.get() : nullptr;
    }

    [[nodiscard]] ClientSession* FindSessionByConversation(std::uint32_t conversation) noexcept
    {
        for (auto& [session_id, session] : sessions_)
        {
            if (session != nullptr && session->conversation() == conversation)
            {
                return session.get();
            }
        }

        return nullptr;
    }

    [[nodiscard]] const ClientSession* FindSessionByConversation(std::uint32_t conversation) const noexcept
    {
        for (const auto& [session_id, session] : sessions_)
        {
            if (session != nullptr && session->conversation() == conversation)
            {
                return session.get();
            }
        }

        return nullptr;
    }

    [[nodiscard]] ClientSession* FindSessionByTransport(
        std::uint32_t conversation,
        const xs::net::Endpoint& remote_endpoint) noexcept
    {
        const auto iterator = transport_index_.find(MakeTransportKey(remote_endpoint, conversation));
        return iterator != transport_index_.end() ? FindSession(iterator->second) : nullptr;
    }

    [[nodiscard]] const ClientSession* FindSessionByTransport(
        std::uint32_t conversation,
        const xs::net::Endpoint& remote_endpoint) const noexcept
    {
        const auto iterator = transport_index_.find(MakeTransportKey(remote_endpoint, conversation));
        return iterator != transport_index_.end() ? FindSession(iterator->second) : nullptr;
    }

    [[nodiscard]] bool RemoveSession(std::uint64_t session_id) noexcept
    {
        const auto iterator = sessions_.find(session_id);
        if (iterator == sessions_.end())
        {
            return false;
        }

        ClientSession* session = iterator->second.get();
        if (session != nullptr)
        {
            transport_index_.erase(MakeTransportKey(session->remote_endpoint(), session->conversation()));
        }
        sessions_.erase(iterator);

        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
            xs::core::LogContextField{"sessionId", std::to_string(session_id)},
            xs::core::LogContextField{"sessionCount", std::to_string(sessions_.size())},
        };
        logger_.Log(xs::core::LogLevel::Info, "client.kcp", "Client session removed.", context);
        return true;
    }

    [[nodiscard]] bool initialized() const noexcept
    {
        return initialized_;
    }

    [[nodiscard]] bool running() const noexcept
    {
        return running_;
    }

    [[nodiscard]] std::size_t session_count() const noexcept
    {
        return sessions_.size();
    }

    [[nodiscard]] std::string_view configured_endpoint() const noexcept
    {
        return options_.listen_endpoint;
    }

    [[nodiscard]] std::string_view last_error_message() const noexcept
    {
        return last_error_message_;
    }

  private:
    [[nodiscard]] std::uint64_t ConsumeNextSessionId() noexcept
    {
        std::uint64_t session_id = next_session_id_ == 0U ? 1U : next_session_id_;
        while (sessions_.contains(session_id))
        {
            ++session_id;
            if (session_id == 0U)
            {
                session_id = 1U;
            }
        }

        next_session_id_ = session_id + 1U;
        if (next_session_id_ == 0U)
        {
            next_session_id_ = 1U;
        }

        return session_id;
    }

    [[nodiscard]] NodeErrorCode StartKcpUpdateTimer()
    {
        if (xs::core::IsTimerID(kcp_update_timer_id_))
        {
            return NodeErrorCode::None;
        }

        const xs::core::TimerCreateResult timer_result =
            event_loop_.timers().CreateRepeating(std::chrono::milliseconds(options_.kcp.interval_ms), [this]() {
                TickKcpSessions();
            });
        if (!xs::core::IsTimerID(timer_result))
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::NodeRunFailed,
                "ClientNetwork failed to schedule KCP update timer: " +
                    std::string(xs::core::TimerErrorMessage(xs::core::TimerErrorFromCreateResult(timer_result))));
        }

        kcp_update_timer_id_ = static_cast<xs::core::TimerID>(timer_result);
        return NodeErrorCode::None;
    }

    void CancelKcpUpdateTimer() noexcept
    {
        if (xs::core::IsTimerID(kcp_update_timer_id_))
        {
            (void)event_loop_.timers().Cancel(kcp_update_timer_id_);
            kcp_update_timer_id_ = 0;
        }
    }

    void StartReceive()
    {
        if (!running_ || socket_ == nullptr || !socket_->is_open())
        {
            return;
        }

        socket_->async_receive_from(
            asio::buffer(receive_buffer_),
            receive_remote_endpoint_,
            [this](const std::error_code& error_code, std::size_t bytes_transferred) {
                HandleReceive(error_code, bytes_transferred);
            });
    }

    void HandleReceive(const std::error_code& error_code, std::size_t bytes_transferred)
    {
        if (!running_)
        {
            return;
        }

        if (error_code)
        {
            if (error_code != asio::error::operation_aborted)
            {
                last_error_message_ = "ClientNetwork UDP receive failed: " + error_code.message();
                const std::array<xs::core::LogContextField, 3> context{
                    xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
                    xs::core::LogContextField{"listenEndpoint", ToEndpointText(options_.listen_endpoint)},
                    xs::core::LogContextField{"udpError", last_error_message_},
                };
                logger_.Log(xs::core::LogLevel::Warn, "client.network", "Client network UDP receive failed.", context);
            }

            StartReceive();
            return;
        }

        HandleIncomingDatagram(
            ToNetEndpoint(receive_remote_endpoint_),
            std::span<const std::byte>(receive_buffer_.data(), bytes_transferred));
        StartReceive();
    }

    void HandleIncomingDatagram(const xs::net::Endpoint& remote_endpoint, std::span<const std::byte> datagram)
    {
        std::uint32_t conversation = 0U;
        if (!TryReadKcpConversation(datagram, &conversation))
        {
            const std::array<xs::core::LogContextField, 3> context{
                xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
                xs::core::LogContextField{"payloadBytes", std::to_string(datagram.size())},
                xs::core::LogContextField{"remoteEndpoint", ToEndpointText(remote_endpoint)},
            };
            logger_.Log(xs::core::LogLevel::Warn, "client.kcp", "Client network ignored a datagram without a readable KCP conversation id.", context);
            return;
        }

        const std::uint64_t now_unix_ms = CurrentUnixTimeMilliseconds();
        const std::uint32_t now_kcp_ms = CurrentKcpClockMilliseconds();

        bool session_created = false;
        std::uint64_t created_session_id = 0U;
        ClientSession* session = FindSessionByTransport(conversation, remote_endpoint);
        if (session == nullptr)
        {
            const NodeErrorCode create_result = CreateSession(conversation, remote_endpoint, &created_session_id, now_unix_ms);
            if (create_result != NodeErrorCode::None)
            {
                const std::array<xs::core::LogContextField, 4> context{
                    xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
                    xs::core::LogContextField{"conversation", std::to_string(conversation)},
                    xs::core::LogContextField{"remoteEndpoint", ToEndpointText(remote_endpoint)},
                    xs::core::LogContextField{"error", last_error_message_},
                };
                logger_.Log(xs::core::LogLevel::Warn, "client.kcp", "Client network failed to create a session for an incoming datagram.", context);
                return;
            }

            session = FindSession(created_session_id);
            session_created = true;
        }

        if (session == nullptr)
        {
            return;
        }

        session->Touch(now_unix_ms);

        const xs::net::KcpPeerErrorCode input_result = session->kcp().Input(datagram);
        if (input_result != xs::net::KcpPeerErrorCode::None)
        {
            const std::string kcp_error = std::string(session->kcp().last_error_message());
            if (session_created)
            {
                (void)RemoveSession(created_session_id);
            }

            const std::array<xs::core::LogContextField, 5> context{
                xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
                xs::core::LogContextField{"conversation", std::to_string(conversation)},
                xs::core::LogContextField{"remoteEndpoint", ToEndpointText(remote_endpoint)},
                xs::core::LogContextField{"payloadBytes", std::to_string(datagram.size())},
                xs::core::LogContextField{"kcpError", kcp_error},
            };
            logger_.Log(xs::core::LogLevel::Warn, "client.kcp", "Client network failed to feed a datagram into KCP.", context);
            return;
        }

        (void)session->kcp().Update(now_kcp_ms);
        FlushPendingDatagrams(*session);

        std::vector<std::byte> payload;
        while (true)
        {
            const xs::net::KcpPeerErrorCode receive_result = session->kcp().Receive(&payload);
            if (receive_result == xs::net::KcpPeerErrorCode::None)
            {
                session->Touch(now_unix_ms);
                const std::array<xs::core::LogContextField, 5> context{
                    xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
                    xs::core::LogContextField{"sessionId", std::to_string(session->session_id())},
                    xs::core::LogContextField{"conversation", std::to_string(session->conversation())},
                    xs::core::LogContextField{"remoteEndpoint", ToEndpointText(session->remote_endpoint())},
                    xs::core::LogContextField{"payloadBytes", std::to_string(payload.size())},
                };
                logger_.Log(xs::core::LogLevel::Info, "client.kcp", "Client network received a client payload.", context);
                continue;
            }

            if (receive_result != xs::net::KcpPeerErrorCode::MessageUnavailable)
            {
                const std::array<xs::core::LogContextField, 5> context{
                    xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
                    xs::core::LogContextField{"sessionId", std::to_string(session->session_id())},
                    xs::core::LogContextField{"conversation", std::to_string(session->conversation())},
                    xs::core::LogContextField{"remoteEndpoint", ToEndpointText(session->remote_endpoint())},
                    xs::core::LogContextField{"kcpError", std::string(session->kcp().last_error_message())},
                };
                logger_.Log(xs::core::LogLevel::Warn, "client.kcp", "Client network failed to decode a KCP payload.", context);
            }
            break;
        }
    }

    void TickKcpSessions()
    {
        if (!running_)
        {
            return;
        }

        const std::uint32_t now_kcp_ms = CurrentKcpClockMilliseconds();
        for (auto& [session_id, session] : sessions_)
        {
            if (session == nullptr)
            {
                continue;
            }

            (void)session->kcp().Update(now_kcp_ms);
            FlushPendingDatagrams(*session);
        }
    }

    void FlushPendingDatagrams(ClientSession& session)
    {
        if (!running_ || socket_ == nullptr || !socket_->is_open())
        {
            (void)session.kcp().ConsumeOutgoingDatagrams();
            return;
        }

        asio::ip::udp::endpoint remote_udp_endpoint;
        std::string endpoint_error;
        if (!TryMakeUdpEndpoint(session.remote_endpoint(), &remote_udp_endpoint, &endpoint_error))
        {
            const std::array<xs::core::LogContextField, 4> context{
                xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
                xs::core::LogContextField{"sessionId", std::to_string(session.session_id())},
                xs::core::LogContextField{"remoteEndpoint", ToEndpointText(session.remote_endpoint())},
                xs::core::LogContextField{"error", endpoint_error},
            };
            logger_.Log(xs::core::LogLevel::Warn, "client.kcp", "Client network failed to resolve a session UDP endpoint.", context);
            (void)session.kcp().ConsumeOutgoingDatagrams();
            return;
        }

        std::vector<std::vector<std::byte>> datagrams = session.kcp().ConsumeOutgoingDatagrams();
        for (const auto& datagram : datagrams)
        {
            std::error_code error_code;
            socket_->send_to(asio::buffer(datagram), remote_udp_endpoint, 0, error_code);
            if (error_code)
            {
                const std::array<xs::core::LogContextField, 5> context{
                    xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
                    xs::core::LogContextField{"sessionId", std::to_string(session.session_id())},
                    xs::core::LogContextField{"conversation", std::to_string(session.conversation())},
                    xs::core::LogContextField{"remoteEndpoint", ToEndpointText(session.remote_endpoint())},
                    xs::core::LogContextField{"udpError", error_code.message()},
                };
                logger_.Log(xs::core::LogLevel::Warn, "client.kcp", "Client network failed to send a UDP datagram to a client session.", context);
                break;
            }
        }
    }

    xs::core::MainEventLoop& event_loop_;
    xs::core::Logger& logger_;
    ClientNetworkOptions options_{};
    xs::net::Endpoint listen_endpoint_{};
    std::map<std::uint64_t, std::unique_ptr<ClientSession>, std::less<>> sessions_{};
    std::map<TransportKey, std::uint64_t, std::less<>> transport_index_{};
    std::unique_ptr<asio::ip::udp::socket> socket_{};
    std::array<std::byte, kMaxUdpDatagramBytes> receive_buffer_{};
    asio::ip::udp::endpoint receive_remote_endpoint_{};
    std::string last_error_message_{};
    std::uint64_t next_session_id_{1U};
    xs::core::TimerID kcp_update_timer_id_{0};
    bool initialized_{false};
    bool running_{false};
};

ClientNetwork::ClientNetwork(
    xs::core::MainEventLoop& event_loop,
    xs::core::Logger& logger,
    ClientNetworkOptions options)
    : impl_(std::make_unique<Impl>(event_loop, logger, std::move(options)))
{
}

ClientNetwork::~ClientNetwork() = default;

NodeErrorCode ClientNetwork::Init()
{
    return impl_->Init();
}

NodeErrorCode ClientNetwork::Run()
{
    return impl_->Run();
}

NodeErrorCode ClientNetwork::Stop()
{
    if (impl_ != nullptr)
    {
        return impl_->Stop();
    }

    return NodeErrorCode::None;
}

NodeErrorCode ClientNetwork::Uninit()
{
    if (impl_ != nullptr)
    {
        return impl_->Uninit();
    }

    return NodeErrorCode::None;
}

NodeErrorCode ClientNetwork::CreateSession(
    std::uint32_t conversation,
    const xs::net::Endpoint& remote_endpoint,
    std::uint64_t* session_id,
    std::uint64_t connected_at_unix_ms)
{
    return impl_ != nullptr
        ? impl_->CreateSession(conversation, remote_endpoint, session_id, connected_at_unix_ms)
        : NodeErrorCode::InvalidArgument;
}

ClientSession* ClientNetwork::FindSession(std::uint64_t session_id) noexcept
{
    return impl_ != nullptr ? impl_->FindSession(session_id) : nullptr;
}

const ClientSession* ClientNetwork::FindSession(std::uint64_t session_id) const noexcept
{
    return impl_ != nullptr ? impl_->FindSession(session_id) : nullptr;
}

ClientSession* ClientNetwork::FindSessionByConversation(std::uint32_t conversation) noexcept
{
    return impl_ != nullptr ? impl_->FindSessionByConversation(conversation) : nullptr;
}

const ClientSession* ClientNetwork::FindSessionByConversation(std::uint32_t conversation) const noexcept
{
    return impl_ != nullptr ? impl_->FindSessionByConversation(conversation) : nullptr;
}

ClientSession* ClientNetwork::FindSessionByTransport(
    std::uint32_t conversation,
    const xs::net::Endpoint& remote_endpoint) noexcept
{
    return impl_ != nullptr ? impl_->FindSessionByTransport(conversation, remote_endpoint) : nullptr;
}

const ClientSession* ClientNetwork::FindSessionByTransport(
    std::uint32_t conversation,
    const xs::net::Endpoint& remote_endpoint) const noexcept
{
    return impl_ != nullptr ? impl_->FindSessionByTransport(conversation, remote_endpoint) : nullptr;
}

bool ClientNetwork::RemoveSession(std::uint64_t session_id) noexcept
{
    return impl_ != nullptr && impl_->RemoveSession(session_id);
}

bool ClientNetwork::initialized() const noexcept
{
    return impl_ != nullptr && impl_->initialized();
}

bool ClientNetwork::running() const noexcept
{
    return impl_ != nullptr && impl_->running();
}

std::size_t ClientNetwork::session_count() const noexcept
{
    return impl_ != nullptr ? impl_->session_count() : 0U;
}

std::string_view ClientNetwork::configured_endpoint() const noexcept
{
    return impl_ != nullptr ? impl_->configured_endpoint() : std::string_view{};
}

std::string_view ClientNetwork::last_error_message() const noexcept
{
    return impl_ != nullptr ? impl_->last_error_message() : std::string_view{};
}

} // namespace xs::node

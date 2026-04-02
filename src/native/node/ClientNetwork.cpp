#include "ClientNetwork.h"

#include "ClientSession.h"
#include "TimeUtils.h"

#include <array>
#include <map>
#include <string>
#include <utility>

namespace xs::node
{
namespace
{

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

std::string ToEndpointText(std::string_view listen_endpoint)
{
    return std::string(listen_endpoint);
}

std::string ToEndpointText(const xs::net::Endpoint& endpoint)
{
    return endpoint.host + ':' + std::to_string(endpoint.port);
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

        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
            xs::core::LogContextField{"listenEndpoint", ToEndpointText(options_.listen_endpoint)},
            xs::core::LogContextField{"sessionCount", std::to_string(sessions_.size())},
        };
        logger_.Log(xs::core::LogLevel::Info, "client.network", "Client network started.", context);

        running_ = true;
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

        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
            xs::core::LogContextField{"listenEndpoint", ToEndpointText(options_.listen_endpoint)},
            xs::core::LogContextField{"sessionCount", std::to_string(sessions_.size())},
        };
        logger_.Log(xs::core::LogLevel::Info, "client.network", "Client network stopped.", context);

        running_ = false;
        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] NodeErrorCode Uninit() noexcept
    {
        sessions_.clear();
        conversation_index_.clear();
        next_session_id_ = 1U;
        running_ = false;
        initialized_ = false;
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

        if (remote_endpoint.host.empty())
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

        if (conversation_index_.contains(conversation))
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "ClientNetwork session conversation is already registered.");
        }

        const std::uint64_t allocated_session_id = ConsumeNextSessionId();
        const std::uint64_t connected_at =
            connected_at_unix_ms != 0U
                ? connected_at_unix_ms
                : static_cast<std::uint64_t>(xs::core::ToUnixTimeMilliseconds(xs::core::UtcNow()));

        auto session = std::make_unique<ClientSession>(
            ClientSessionOptions{
                .gate_node_id = options_.owner_node_id,
                .session_id = allocated_session_id,
                .conversation = conversation,
                .remote_endpoint = remote_endpoint,
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

        conversation_index_.emplace(conversation, allocated_session_id);
        sessions_.emplace(allocated_session_id, std::move(session));
        if (session_id != nullptr)
        {
            *session_id = allocated_session_id;
        }

        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
            xs::core::LogContextField{"sessionId", std::to_string(allocated_session_id)},
            xs::core::LogContextField{"conversation", std::to_string(conversation)},
            xs::core::LogContextField{"remoteEndpoint", ToEndpointText(remote_endpoint)},
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
        const auto iterator = conversation_index_.find(conversation);
        return iterator != conversation_index_.end() ? FindSession(iterator->second) : nullptr;
    }

    [[nodiscard]] const ClientSession* FindSessionByConversation(std::uint32_t conversation) const noexcept
    {
        const auto iterator = conversation_index_.find(conversation);
        return iterator != conversation_index_.end() ? FindSession(iterator->second) : nullptr;
    }

    [[nodiscard]] bool RemoveSession(std::uint64_t session_id) noexcept
    {
        const auto iterator = sessions_.find(session_id);
        if (iterator == sessions_.end())
        {
            return false;
        }

        const std::uint32_t conversation = iterator->second->conversation();
        conversation_index_.erase(conversation);
        sessions_.erase(iterator);

        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"ownerNodeId", options_.owner_node_id},
            xs::core::LogContextField{"sessionId", std::to_string(session_id)},
            xs::core::LogContextField{"conversation", std::to_string(conversation)},
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

    xs::core::MainEventLoop& event_loop_;
    xs::core::Logger& logger_;
    ClientNetworkOptions options_{};
    std::map<std::uint64_t, std::unique_ptr<ClientSession>, std::less<>> sessions_{};
    std::map<std::uint32_t, std::uint64_t, std::less<>> conversation_index_{};
    std::string last_error_message_{};
    std::uint64_t next_session_id_{1U};
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

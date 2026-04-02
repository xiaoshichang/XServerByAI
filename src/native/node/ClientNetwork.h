#pragma once

#include "NodeCommon.h"

#include "Config.h"
#include "Logging.h"
#include "MainEventLoop.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace xs::net
{

struct Endpoint;

}

namespace xs::node
{

class ClientSession;

struct ClientNetworkOptions
{
    std::string owner_node_id{};
    std::string listen_endpoint{};
    xs::core::KcpConfig kcp{};
};

class ClientNetwork final
{
  public:
    ClientNetwork(
        xs::core::MainEventLoop& event_loop,
        xs::core::Logger& logger,
        ClientNetworkOptions options = {});
    ~ClientNetwork();

    ClientNetwork(const ClientNetwork&) = delete;
    ClientNetwork& operator=(const ClientNetwork&) = delete;
    ClientNetwork(ClientNetwork&&) = delete;
    ClientNetwork& operator=(ClientNetwork&&) = delete;

    [[nodiscard]] NodeErrorCode Init();
    [[nodiscard]] NodeErrorCode Run();
    [[nodiscard]] NodeErrorCode Stop();
    [[nodiscard]] NodeErrorCode Uninit();
    [[nodiscard]] NodeErrorCode CreateSession(
        std::uint32_t conversation,
        const xs::net::Endpoint& remote_endpoint,
        std::uint64_t* session_id = nullptr,
        std::uint64_t connected_at_unix_ms = 0U);
    [[nodiscard]] ClientSession* FindSession(std::uint64_t session_id) noexcept;
    [[nodiscard]] const ClientSession* FindSession(std::uint64_t session_id) const noexcept;
    [[nodiscard]] ClientSession* FindSessionByConversation(std::uint32_t conversation) noexcept;
    [[nodiscard]] const ClientSession* FindSessionByConversation(std::uint32_t conversation) const noexcept;
    [[nodiscard]] ClientSession* FindSessionByTransport(
        std::uint32_t conversation,
        const xs::net::Endpoint& remote_endpoint) noexcept;
    [[nodiscard]] const ClientSession* FindSessionByTransport(
        std::uint32_t conversation,
        const xs::net::Endpoint& remote_endpoint) const noexcept;
    [[nodiscard]] bool RemoveSession(std::uint64_t session_id) noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::size_t session_count() const noexcept;
    [[nodiscard]] std::string_view configured_endpoint() const noexcept;
    [[nodiscard]] std::string_view last_error_message() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xs::node
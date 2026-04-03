#pragma once

#include "NodeCommon.h"

#include "Config.h"
#include "Logging.h"
#include "MainEventLoop.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace xs::node
{

struct GateAuthLoginRequest final
{
    std::string account{};
    std::string password{};
    std::string remote_address{};
};

struct GateAuthLoginResult final
{
    bool success{false};
    std::uint16_t status_code{200};
    std::string error{};
    std::string gate_node_id{};
    std::string kcp_host{};
    std::uint16_t kcp_port{0U};
    std::uint32_t conversation{0U};
    std::uint64_t issued_at_unix_ms{0U};
    std::uint64_t expires_at_unix_ms{0U};
};

using GateAuthLoginHandler = std::function<GateAuthLoginResult(const GateAuthLoginRequest&)>;

struct GateAuthHttpServiceOptions
{
    xs::core::EndpointConfig listen_endpoint{};
    std::string node_id{};
    GateAuthLoginHandler login_handler{};
};

class GateAuthHttpService final
{
  public:
    GateAuthHttpService(
        xs::core::MainEventLoop& event_loop,
        xs::core::Logger& logger,
        GateAuthHttpServiceOptions options = {});
    ~GateAuthHttpService();

    GateAuthHttpService(const GateAuthHttpService&) = delete;
    GateAuthHttpService& operator=(const GateAuthHttpService&) = delete;
    GateAuthHttpService(GateAuthHttpService&&) = delete;
    GateAuthHttpService& operator=(GateAuthHttpService&&) = delete;

    [[nodiscard]] NodeErrorCode Init();
    [[nodiscard]] NodeErrorCode Run();
    [[nodiscard]] NodeErrorCode Uninit();

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::string_view configured_endpoint() const noexcept;
    [[nodiscard]] std::string_view bound_endpoint() const noexcept;
    [[nodiscard]] std::string_view last_error_message() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xs::node
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

struct GmControlHttpResponse
{
    std::uint16_t status_code{200};
    std::string content_type{"application/json; charset=utf-8"};
    std::string body{};
    bool request_stop{false};
};

struct GmControlHttpStatusSnapshot
{
    std::string inner_network_endpoint{};
    std::uint64_t registered_process_count{0};
    bool running{false};
};

using GmControlHttpStatusProvider = std::function<GmControlHttpStatusSnapshot()>;
using GmControlHttpStopHandler = std::function<void()>;

struct GmControlHttpServiceOptions
{
    xs::core::EndpointConfig listen_endpoint{};
    std::string node_id{};
    GmControlHttpStatusProvider status_provider{};
    GmControlHttpStopHandler stop_handler{};
};

class GmControlHttpService final
{
  public:
    GmControlHttpService(
        xs::core::MainEventLoop& event_loop,
        xs::core::Logger& logger,
        GmControlHttpServiceOptions options = {});
    ~GmControlHttpService();

    GmControlHttpService(const GmControlHttpService&) = delete;
    GmControlHttpService& operator=(const GmControlHttpService&) = delete;
    GmControlHttpService(GmControlHttpService&&) = delete;
    GmControlHttpService& operator=(GmControlHttpService&&) = delete;

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

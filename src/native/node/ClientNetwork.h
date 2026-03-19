#pragma once

#include "NodeCommon.h"

#include "Logging.h"
#include "MainEventLoop.h"

#include <memory>
#include <string_view>

namespace xs::node
{

class ClientNetwork final
{
  public:
    ClientNetwork(xs::core::MainEventLoop& event_loop, xs::core::Logger& logger);
    ~ClientNetwork();

    ClientNetwork(const ClientNetwork&) = delete;
    ClientNetwork& operator=(const ClientNetwork&) = delete;
    ClientNetwork(ClientNetwork&&) = delete;
    ClientNetwork& operator=(ClientNetwork&&) = delete;

    [[nodiscard]] NodeErrorCode Init();
    [[nodiscard]] NodeErrorCode Run();
    [[nodiscard]] NodeErrorCode Uninit();

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::string_view last_error_message() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xs::node

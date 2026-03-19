#pragma once

#include "NodeRuntime.h"

#include <memory>

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

    [[nodiscard]] NodeRuntimeErrorCode Init(std::string* error_message = nullptr);
    [[nodiscard]] NodeRuntimeErrorCode Run(std::string* error_message = nullptr);
    void Uninit() noexcept;

    [[nodiscard]] bool initialized() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xs::node

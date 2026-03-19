#pragma once

#include "NodeRuntime.h"

namespace xs::node
{

struct ServerNodeEnvironment
{
    const NodeRuntimeContext& context;
    xs::core::Logger& logger;
    xs::core::MainEventLoop& event_loop;
};

class ServerNode
{
  public:
    explicit ServerNode(ServerNodeEnvironment environment);
    virtual ~ServerNode();

    ServerNode(const ServerNode&) = delete;
    ServerNode& operator=(const ServerNode&) = delete;
    ServerNode(ServerNode&&) = delete;
    ServerNode& operator=(ServerNode&&) = delete;

    [[nodiscard]] virtual NodeRuntimeErrorCode Init(std::string* error_message) = 0;
    [[nodiscard]] virtual NodeRuntimeErrorCode Run(std::string* error_message) = 0;
    virtual void Uninit() noexcept = 0;

  protected:
    [[nodiscard]] const NodeRuntimeContext& context() const noexcept;
    [[nodiscard]] xs::core::Logger& logger() const noexcept;
    [[nodiscard]] xs::core::MainEventLoop& event_loop() const noexcept;

  private:
    const NodeRuntimeContext& context_;
    xs::core::Logger& logger_;
    xs::core::MainEventLoop& event_loop_;
};

} // namespace xs::node

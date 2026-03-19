#pragma once

#include "InnerNetwork.h"
#include "ServerNode.h"

#include <memory>

namespace xs::node
{

class GameNode final : public ServerNode
{
  public:
    explicit GameNode(ServerNodeEnvironment environment);
    ~GameNode() override;

    [[nodiscard]] NodeRuntimeErrorCode Init(std::string* error_message) override;
    [[nodiscard]] NodeRuntimeErrorCode Run(std::string* error_message) override;
    void Uninit() noexcept override;

  private:
    std::unique_ptr<InnerNetwork> inner_network_{};
    bool initialized_{false};
};

} // namespace xs::node

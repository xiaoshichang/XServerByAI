#pragma once

#include "ClientNetwork.h"
#include "InnerNetwork.h"
#include "ServerNode.h"

#include <memory>

namespace xs::node
{

class GateNode final : public ServerNode
{
  public:
    explicit GateNode(ServerNodeEnvironment environment);
    ~GateNode() override;

    [[nodiscard]] NodeRuntimeErrorCode Init(std::string* error_message) override;
    [[nodiscard]] NodeRuntimeErrorCode Run(std::string* error_message) override;
    void Uninit() noexcept override;

  private:
    std::unique_ptr<InnerNetwork> inner_network_{};
    std::unique_ptr<ClientNetwork> client_network_{};
    bool initialized_{false};
};

} // namespace xs::node

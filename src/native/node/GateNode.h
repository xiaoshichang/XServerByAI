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
    explicit GateNode(NodeCommandLineArgs args);
    ~GateNode() override;

  protected:
    [[nodiscard]] xs::core::ProcessType role_process_type() const noexcept override;
    [[nodiscard]] NodeErrorCode OnInit() override;
    [[nodiscard]] NodeErrorCode OnRun() override;
    [[nodiscard]] NodeErrorCode OnUninit() override;

  private:
    std::unique_ptr<InnerNetwork> inner_network_{};
    std::unique_ptr<ClientNetwork> client_network_{};
};

} // namespace xs::node

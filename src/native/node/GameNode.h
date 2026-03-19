#pragma once

#include "InnerNetwork.h"
#include "ServerNode.h"

#include <memory>

namespace xs::node
{

class GameNode final : public ServerNode
{
  public:
    explicit GameNode(NodeCommandLineArgs args);
    ~GameNode() override;

  protected:
    [[nodiscard]] xs::core::ProcessType role_process_type() const noexcept override;
    [[nodiscard]] NodeErrorCode OnInit() override;
    [[nodiscard]] NodeErrorCode OnRun() override;
    [[nodiscard]] NodeErrorCode OnUninit() override;

  private:
    std::unique_ptr<InnerNetwork> inner_network_{};
};

} // namespace xs::node

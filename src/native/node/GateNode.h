#pragma once

#include "ClientNetwork.h"
#include "InnerNetwork.h"
#include "ServerNode.h"

#include <memory>
#include <span>
#include <vector>

namespace xs::node
{

class GateNode final : public ServerNode
{
  public:
    explicit GateNode(NodeCommandLineArgs args);
    ~GateNode() override;

    [[nodiscard]] std::string_view gm_inner_remote_endpoint() const noexcept;
    [[nodiscard]] std::string_view configured_inner_endpoint() const noexcept;
    [[nodiscard]] ipc::ZmqConnectionState gm_inner_connection_state() const noexcept;

  protected:
    [[nodiscard]] xs::core::ProcessType role_process_type() const noexcept override;
    [[nodiscard]] NodeErrorCode OnInit() override;
    [[nodiscard]] NodeErrorCode OnRun() override;
    [[nodiscard]] NodeErrorCode OnUninit() override;

  private:
    void HandleInnerMessage(std::span<const std::byte> payload);

    std::string gm_inner_remote_endpoint_{};
    std::string configured_inner_endpoint_{};
    std::unique_ptr<InnerNetwork> inner_network_{};
    std::unique_ptr<ClientNetwork> client_network_{};
};

} // namespace xs::node

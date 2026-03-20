#pragma once

#include "InnerNetwork.h"
#include "ProcessRegistry.h"
#include "ServerNode.h"

#include <memory>
#include <span>
#include <vector>

namespace xs::node
{

class GmNode final : public ServerNode
{
  public:
    explicit GmNode(NodeCommandLineArgs args);
    ~GmNode() override;

    [[nodiscard]] std::vector<ProcessRegistryEntry> registry_snapshot() const;

  protected:
    [[nodiscard]] xs::core::ProcessType role_process_type() const noexcept override;
    [[nodiscard]] NodeErrorCode OnInit() override;
    [[nodiscard]] NodeErrorCode OnRun() override;
    [[nodiscard]] NodeErrorCode OnUninit() override;

  private:
    void HandleControlMessage(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);

    std::unique_ptr<InnerNetwork> inner_network_{};
    ProcessRegistry process_registry_{};
};

} // namespace xs::node

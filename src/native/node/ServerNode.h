#pragma once

#include "NodeCommon.h"

#include "Config.h"
#include "Logging.h"
#include "MainEventLoop.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace xs::node
{

class ServerNode
{
  public:
    explicit ServerNode(NodeCommandLineArgs args);
    virtual ~ServerNode();

    ServerNode(const ServerNode&) = delete;
    ServerNode& operator=(const ServerNode&) = delete;
    ServerNode(ServerNode&&) = delete;
    ServerNode& operator=(ServerNode&&) = delete;

    [[nodiscard]] NodeErrorCode Init();
    [[nodiscard]] NodeErrorCode Run();
    [[nodiscard]] NodeErrorCode Uninit();
    void RequestStop() noexcept;

    [[nodiscard]] const std::filesystem::path& config_path() const noexcept;
    [[nodiscard]] std::string_view selector() const noexcept;
    [[nodiscard]] xs::core::ProcessType process_type() const noexcept;
    [[nodiscard]] std::string_view node_id() const noexcept;
    [[nodiscard]] std::uint32_t pid() const noexcept;
    [[nodiscard]] const xs::core::NodeConfig& node_config() const noexcept;
    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::string_view last_error_message() const noexcept;

  protected:
    [[nodiscard]] xs::core::Logger& logger() const noexcept;
    [[nodiscard]] xs::core::MainEventLoop& event_loop() const noexcept;
    [[nodiscard]] NodeErrorCode SetError(NodeErrorCode code, std::string message = {});
    void ClearError() noexcept;

    [[nodiscard]] virtual xs::core::ProcessType role_process_type() const noexcept = 0;
    [[nodiscard]] virtual NodeErrorCode OnInit() = 0;
    [[nodiscard]] virtual NodeErrorCode OnRun() = 0;
    [[nodiscard]] virtual NodeErrorCode OnUninit() = 0;

  private:
    void ReleaseCoreState() noexcept;

    NodeCommandLineArgs args_{};
    xs::core::ProcessType process_type_{xs::core::ProcessType::Gm};
    std::string node_id_{};
    std::uint32_t pid_{0U};
    xs::core::NodeConfig node_config_{};
    std::unique_ptr<xs::core::Logger> logger_{};
    std::unique_ptr<xs::core::MainEventLoop> event_loop_{};
    std::string last_error_message_{};
    bool initialized_{false};
};

} // namespace xs::node

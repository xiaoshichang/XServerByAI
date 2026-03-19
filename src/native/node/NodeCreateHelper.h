#pragma once

#include "NodeCommon.h"

#include <memory>
#include <string>
#include <string_view>

namespace xs::node
{

class ServerNode;
using ServerNodePtr = std::unique_ptr<ServerNode>;

class NodeCreateHelper final
{
  public:
    NodeCreateHelper() = default;
    explicit NodeCreateHelper(NodeCommandLineArgs args);

    [[nodiscard]] NodeErrorCode ParseCommandLine(int argc, char* argv[]);
    [[nodiscard]] NodeErrorCode CreateNode(ServerNodePtr* output);

    [[nodiscard]] const NodeCommandLineArgs& args() const noexcept;
    [[nodiscard]] std::string_view last_error_message() const noexcept;

  private:
    void ClearError() noexcept;
    [[nodiscard]] NodeErrorCode SetError(NodeErrorCode code, std::string message = {});

    NodeCommandLineArgs args_{};
    std::string last_error_message_{};
};

} // namespace xs::node

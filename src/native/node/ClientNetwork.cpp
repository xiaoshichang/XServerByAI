#include "ClientNetwork.h"

#include <string>
#include <utility>

namespace xs::node
{
namespace
{

void ClearError(std::string& error_message) noexcept
{
    error_message.clear();
}

NodeErrorCode SetError(
    std::string& error_message,
    NodeErrorCode code,
    std::string message)
{
    if (message.empty())
    {
        error_message = std::string(NodeErrorMessage(code));
    }
    else
    {
        error_message = std::move(message);
    }

    return code;
}

} // namespace

class ClientNetwork::Impl final
{
  public:
    Impl(xs::core::MainEventLoop& event_loop, xs::core::Logger& logger)
        : event_loop_(event_loop), logger_(logger)
    {
    }

    [[nodiscard]] NodeErrorCode Init()
    {
        if (initialized_)
        {
            return SetError(last_error_message_, NodeErrorCode::InvalidArgument, "ClientNetwork is already initialized.");
        }

        initialized_ = true;
        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] NodeErrorCode Run()
    {
        if (!initialized_)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "ClientNetwork must be initialized before Run().");
        }

        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] NodeErrorCode Uninit() noexcept
    {
        initialized_ = false;
        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] bool initialized() const noexcept
    {
        return initialized_;
    }

    [[nodiscard]] std::string_view last_error_message() const noexcept
    {
        return last_error_message_;
    }

  private:
    xs::core::MainEventLoop& event_loop_;
    xs::core::Logger& logger_;
    std::string last_error_message_{};
    bool initialized_{false};
};

ClientNetwork::ClientNetwork(xs::core::MainEventLoop& event_loop, xs::core::Logger& logger)
    : impl_(std::make_unique<Impl>(event_loop, logger))
{
}

ClientNetwork::~ClientNetwork() = default;

NodeErrorCode ClientNetwork::Init()
{
    return impl_->Init();
}

NodeErrorCode ClientNetwork::Run()
{
    return impl_->Run();
}

NodeErrorCode ClientNetwork::Uninit()
{
    if (impl_ != nullptr)
    {
        return impl_->Uninit();
    }

    return NodeErrorCode::None;
}

bool ClientNetwork::initialized() const noexcept
{
    return impl_ != nullptr && impl_->initialized();
}

std::string_view ClientNetwork::last_error_message() const noexcept
{
    return impl_ != nullptr ? impl_->last_error_message() : std::string_view{};
}

} // namespace xs::node

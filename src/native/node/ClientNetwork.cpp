#include "ClientNetwork.h"

namespace xs::node
{
namespace
{

void ClearError(std::string* error_message)
{
    if (error_message != nullptr)
    {
        error_message->clear();
    }
}

} // namespace

class ClientNetwork::Impl final
{
  public:
    Impl(xs::core::MainEventLoop& event_loop, xs::core::Logger& logger)
        : event_loop_(event_loop), logger_(logger)
    {
    }

    [[nodiscard]] NodeRuntimeErrorCode Init(std::string* error_message)
    {
        initialized_ = true;
        ClearError(error_message);
        return NodeRuntimeErrorCode::None;
    }

    [[nodiscard]] NodeRuntimeErrorCode Run(std::string* error_message)
    {
        ClearError(error_message);
        return NodeRuntimeErrorCode::None;
    }

    void Uninit() noexcept
    {
        initialized_ = false;
    }

    [[nodiscard]] bool initialized() const noexcept
    {
        return initialized_;
    }

  private:
    xs::core::MainEventLoop& event_loop_;
    xs::core::Logger& logger_;
    bool initialized_{false};
};

ClientNetwork::ClientNetwork(xs::core::MainEventLoop& event_loop, xs::core::Logger& logger)
    : impl_(std::make_unique<Impl>(event_loop, logger))
{
}

ClientNetwork::~ClientNetwork() = default;

NodeRuntimeErrorCode ClientNetwork::Init(std::string* error_message)
{
    return impl_->Init(error_message);
}

NodeRuntimeErrorCode ClientNetwork::Run(std::string* error_message)
{
    return impl_->Run(error_message);
}

void ClientNetwork::Uninit() noexcept
{
    if (impl_ != nullptr)
    {
        impl_->Uninit();
    }
}

bool ClientNetwork::initialized() const noexcept
{
    return impl_ != nullptr && impl_->initialized();
}

} // namespace xs::node

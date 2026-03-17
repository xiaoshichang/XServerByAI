#include "ZmqContext.h"

#include <zmq.h>

#include <utility>

namespace xs::ipc
{
namespace
{

[[nodiscard]] std::string BuildZmqErrorMessage(std::string_view prefix, int error_code)
{
    std::string message(prefix);
    message += ": ";
    message += zmq_strerror(error_code);
    return message;
}

} // namespace

class ZmqContext::Impl final
{
  public:
    explicit Impl(ZmqContextOptions options)
        : options_(std::move(options))
    {
        Initialize();
    }

    ~Impl()
    {
        if (context_ != nullptr)
        {
            (void)zmq_ctx_shutdown(context_);
            (void)zmq_ctx_term(context_);
        }
    }

    [[nodiscard]] bool IsValid() const noexcept
    {
        return context_ != nullptr;
    }

    [[nodiscard]] const ZmqContextOptions& options() const noexcept
    {
        return options_;
    }

    [[nodiscard]] std::string_view initialization_error() const noexcept
    {
        return initialization_error_;
    }

    [[nodiscard]] void* native_handle() const noexcept
    {
        return context_;
    }

  private:
    void Initialize()
    {
        if (options_.io_threads < 1)
        {
            initialization_error_ = "ZeroMQ io_threads must be greater than zero.";
            return;
        }

        if (options_.max_sockets < 1)
        {
            initialization_error_ = "ZeroMQ max_sockets must be greater than zero.";
            return;
        }

        context_ = zmq_ctx_new();
        if (context_ == nullptr)
        {
            initialization_error_ = BuildZmqErrorMessage("Failed to create ZeroMQ context", zmq_errno());
            return;
        }

        if (zmq_ctx_set(context_, ZMQ_IO_THREADS, options_.io_threads) != 0)
        {
            initialization_error_ = BuildZmqErrorMessage("Failed to configure ZeroMQ io_threads", zmq_errno());
            (void)zmq_ctx_term(context_);
            context_ = nullptr;
            return;
        }

        if (zmq_ctx_set(context_, ZMQ_MAX_SOCKETS, options_.max_sockets) != 0)
        {
            initialization_error_ = BuildZmqErrorMessage("Failed to configure ZeroMQ max_sockets", zmq_errno());
            (void)zmq_ctx_term(context_);
            context_ = nullptr;
        }
    }

    ZmqContextOptions options_{};
    void* context_{nullptr};
    std::string initialization_error_{};
};

ZmqContext::ZmqContext(ZmqContextOptions options)
    : impl_(std::make_unique<Impl>(std::move(options)))
{
}

ZmqContext::~ZmqContext() = default;

bool ZmqContext::IsValid() const noexcept
{
    return impl_ != nullptr && impl_->IsValid();
}

const ZmqContextOptions& ZmqContext::options() const noexcept
{
    return impl_->options();
}

std::string_view ZmqContext::initialization_error() const noexcept
{
    return impl_ != nullptr ? impl_->initialization_error() : std::string_view{};
}

void* ZmqContext::native_handle() const noexcept
{
    return impl_ != nullptr ? impl_->native_handle() : nullptr;
}

} // namespace xs::ipc

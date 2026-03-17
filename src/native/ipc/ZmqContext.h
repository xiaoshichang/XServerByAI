#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace xs::ipc
{

struct ZmqContextOptions
{
    int io_threads{1};
    int max_sockets{1023};
};

class ZmqContext final
{
  public:
    explicit ZmqContext(ZmqContextOptions options = {});
    ~ZmqContext();

    ZmqContext(const ZmqContext&) = delete;
    ZmqContext& operator=(const ZmqContext&) = delete;
    ZmqContext(ZmqContext&&) = delete;
    ZmqContext& operator=(ZmqContext&&) = delete;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] const ZmqContextOptions& options() const noexcept;
    [[nodiscard]] std::string_view initialization_error() const noexcept;
    [[nodiscard]] void* native_handle() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xs::ipc

#include "ZmqActiveConnector.h"
#include "ZmqContext.h"

#include <asio/io_context.hpp>

#include <zmq.h>

#include <type_traits>

static_assert(ZMQ_VERSION == 40305, "Vendored ZeroMQ version must remain 4.3.5");
static_assert(std::is_default_constructible_v<xs::ipc::ZmqContextOptions>, "ZmqContextOptions must remain default constructible.");
static_assert(
    std::is_default_constructible_v<xs::ipc::ZmqActiveConnectorOptions>,
    "ZmqActiveConnectorOptions must remain default constructible.");
static_assert(std::is_constructible_v<xs::ipc::ZmqContext, xs::ipc::ZmqContextOptions>, "ZmqContext must remain options-based.");
static_assert(
    std::is_constructible_v<xs::ipc::ZmqActiveConnector,
                            asio::io_context&,
                            xs::ipc::ZmqContext&,
                            xs::ipc::ZmqActiveConnectorOptions>,
    "ZmqActiveConnector must remain constructible from io_context, context and options.");

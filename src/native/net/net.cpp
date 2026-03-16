#include "PacketHeader.h"

#include <asio/io_context.hpp>
#include <asio/version.hpp>

#include <type_traits>

#ifndef ASIO_STANDALONE
#error "Standalone Asio integration must define ASIO_STANDALONE."
#endif

static_assert(ASIO_VERSION == 103600, "Vendored Asio version must remain 1.36.0");
static_assert(std::is_standard_layout_v<xs::net::PacketHeader>, "PacketHeader must remain standard layout");
static_assert(std::is_default_constructible_v<asio::io_context>, "Asio io_context must remain available");
static_assert(xs::net::kPacketMagic == 0x47535052u, "Packet magic must match the protocol spec");

namespace xs::net {
void placeholder() {}
}
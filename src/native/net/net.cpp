#include "BinarySerialization.h"
#include "ByteOrder.h"
#include "MessageDispatcher.h"
#include "PacketHeader.h"
#include "message/HeartbeatCodec.h"
#include "message/PacketCodec.h"

#include <asio/io_context.hpp>
#include <asio/version.hpp>

#include <span>
#include <type_traits>

#ifndef ASIO_STANDALONE
#error "Standalone Asio integration must define ASIO_STANDALONE."
#endif

static_assert(ASIO_VERSION == 103600, "Vendored Asio version must remain 1.36.0");
static_assert(std::is_standard_layout_v<xs::net::PacketHeader>, "PacketHeader must remain standard layout");
static_assert(std::is_constructible_v<xs::net::BinaryWriter, std::span<std::byte>>, "BinaryWriter must remain span-based.");
static_assert(std::is_constructible_v<xs::net::BinaryReader, std::span<const std::byte>>, "BinaryReader must remain span-based.");
static_assert(std::is_default_constructible_v<xs::net::PacketView>, "PacketView must remain default constructible.");
static_assert(std::is_default_constructible_v<xs::net::MessageDispatcher>, "MessageDispatcher must remain default constructible.");
static_assert(std::is_copy_constructible_v<xs::net::MessageHandler>, "MessageHandler must remain copy constructible.");
static_assert(std::is_default_constructible_v<xs::net::LoadSnapshot>, "LoadSnapshot must remain default constructible.");
static_assert(std::is_default_constructible_v<xs::net::HeartbeatRequest>, "HeartbeatRequest must remain default constructible.");
static_assert(std::is_default_constructible_v<xs::net::HeartbeatSuccessResponse>, "HeartbeatSuccessResponse must remain default constructible.");
static_assert(std::is_default_constructible_v<xs::net::HeartbeatErrorResponse>, "HeartbeatErrorResponse must remain default constructible.");
static_assert(
    xs::net::NetworkToHost(xs::net::HostToNetwork<std::uint64_t>(0x0102030405060708ull)) == 0x0102030405060708ull,
    "Byte-order conversion must round-trip fixed-width integers.");
static_assert(std::is_default_constructible_v<asio::io_context>, "Asio io_context must remain available");
static_assert(xs::net::kPacketMagic == 0x47535052u, "Packet magic must match the protocol spec");
static_assert(xs::net::kControlHeartbeatMsgId == 1100u, "Heartbeat msgId must match the control-plane spec");

namespace xs::net
{
void placeholder()
{
}
} // namespace xs::net
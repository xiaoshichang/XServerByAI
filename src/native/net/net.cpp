#include "PacketHeader.h"

#include <type_traits>

static_assert(std::is_standard_layout_v<xs::net::PacketHeader>, "PacketHeader must remain standard layout");
static_assert(xs::net::kPacketMagic == 0x47535052u, "Packet magic must match the protocol spec");

namespace xs::net {
void placeholder() {}
}

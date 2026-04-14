#pragma once

#include <cstdint>

namespace xs::net
{

// Inner registration and heartbeat
inline constexpr std::uint32_t kInnerRegisterMsgId = 1000u;
inline constexpr std::uint32_t kInnerHeartbeatMsgId = 1100u;

// Inner cluster lifecycle
inline constexpr std::uint32_t kInnerClusterReadyNotifyMsgId = 1201u;
inline constexpr std::uint32_t kInnerServerStubOwnershipSyncMsgId = 1202u;
inline constexpr std::uint32_t kInnerGameServiceReadyReportMsgId = 1203u;
inline constexpr std::uint32_t kInnerClusterNodesOnlineNotifyMsgId = 1204u;
inline constexpr std::uint32_t kInnerGameGateMeshReadyReportMsgId = 1205u;

// Gate <-> Game relay and control
inline constexpr std::uint32_t kRelayForwardToGameMsgId = 2000u;
inline constexpr std::uint32_t kRelayPushToClientMsgId = 2001u;
inline constexpr std::uint32_t kRelayForwardMailboxCallMsgId = 2002u;
inline constexpr std::uint32_t kGateCreateAvatarEntityMsgId = 2003u;
inline constexpr std::uint32_t kGameAvatarEntityCreateResultMsgId = 2004u;
inline constexpr std::uint32_t kRelayForwardProxyCallMsgId = 2005u;

// Client <-> Server stable messages
inline constexpr std::uint32_t kClientHelloMsgId = 45010u;
inline constexpr std::uint32_t kClientMoveMsgId = 45011u;
inline constexpr std::uint32_t kClientSelectAvatarMsgId = 45013u;
inline constexpr std::uint32_t kServerBroadcastMessageMsgId = 6201u;
inline constexpr std::uint32_t kClientEntityRpcMsgId = 6302u;
inline constexpr std::uint32_t kServerToClientEntityRpcMsgId = 6303u;

// Current gameplay / stub sample messages
inline constexpr std::uint32_t kMatchStubStartupCallMsgId = 5101u;
inline constexpr std::uint32_t kOnlineStubRegisterAvatarMsgId = 5200u;
inline constexpr std::uint32_t kOnlineStubBroadcastMsgId = 5201u;
inline constexpr std::uint32_t kOnlineAvatarBroadcastProxyMsgId = kServerBroadcastMessageMsgId;

} // namespace xs::net

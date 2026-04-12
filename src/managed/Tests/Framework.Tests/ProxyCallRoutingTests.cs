using XServer.Managed.Framework.Entities;
using XServer.Managed.Framework.Runtime;
using XServer.Managed.GameLogic.Services;

namespace XServer.Managed.Framework.Tests
{
    public class ProxyCallRoutingTests
    {
        [Fact]
        public void AvatarEntity_CallProxy_PrefersLocalEntityDispatch()
        {
            LoopbackServerEntityMessageTransport transport = new();
            GameNodeRuntimeState runtimeState = new(
                "Game0",
                messageTransport: transport);
            transport.Register(runtimeState);

            ServerStubOwnershipSnapshot snapshot = new(
                1,
                [
                    new ServerStubOwnershipAssignment(nameof(OnlineStub), "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(snapshot));

            Guid sourceAvatarId = Guid.NewGuid();
            Guid targetAvatarId = Guid.NewGuid();
            Assert.True(runtimeState.TryCreateAvatarEntity(
                new AvatarEntitySpawnRequest(sourceAvatarId, "account-1", "Gate0", 100),
                out AvatarEntity? sourceAvatar,
                out string? sourceError));
            Assert.Null(sourceError);
            Assert.True(runtimeState.TryCreateAvatarEntity(
                new AvatarEntitySpawnRequest(targetAvatarId, "account-2", "Gate0", 101),
                out AvatarEntity? targetAvatar,
                out string? targetError));
            Assert.Null(targetError);

            byte[] payload = [0x01, 0x23, 0x45];
            sourceAvatar!.CallProxy(targetAvatar!.Proxy!, OnlineStub.BroadcastOnlineAvatarProxyMsgId, payload);

            Assert.Equal(0, transport.ServerProxyForwardCallCount);
            Assert.Equal(1, transport.ClientProxyForwardCallCount);
            Assert.Equal(targetAvatar.EntityId, transport.LastClientProxyTargetEntityId);
            Assert.Equal("Gate0", transport.LastClientProxyRouteGateNodeId);
            Assert.Collection(
                targetAvatar.ReceivedProxyMessages,
                received =>
                {
                    Assert.Equal(OnlineStub.BroadcastOnlineAvatarProxyMsgId, received.MsgId);
                    Assert.Equal(payload, received.Payload);
                });
        }

        [Fact]
        public void AvatarEntity_CallProxy_ForwardsToRemoteEntity_WhenLocalLookupMisses()
        {
            LoopbackServerEntityMessageTransport transport = new();
            GameNodeRuntimeState onlineRuntime = new("Game9", messageTransport: transport);
            GameNodeRuntimeState sourceRuntime = new("Game0", messageTransport: transport);
            GameNodeRuntimeState targetRuntime = new("Game1", messageTransport: transport);
            transport.Register(onlineRuntime);
            transport.Register(sourceRuntime);
            transport.Register(targetRuntime);

            ServerStubOwnershipSnapshot snapshot = new(
                1,
                [
                    new ServerStubOwnershipAssignment(nameof(OnlineStub), "unknown", "Game9"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, onlineRuntime.ApplyOwnership(snapshot));
            Assert.Equal(GameNodeRuntimeStateErrorCode.None, sourceRuntime.ApplyOwnership(snapshot));
            Assert.Equal(GameNodeRuntimeStateErrorCode.None, targetRuntime.ApplyOwnership(snapshot));

            Guid sourceAvatarId = Guid.NewGuid();
            Guid targetAvatarId = Guid.NewGuid();
            Assert.True(sourceRuntime.TryCreateAvatarEntity(
                new AvatarEntitySpawnRequest(sourceAvatarId, "account-1", "Gate0", 100),
                out AvatarEntity? sourceAvatar,
                out string? sourceError));
            Assert.Null(sourceError);
            Assert.True(targetRuntime.TryCreateAvatarEntity(
                new AvatarEntitySpawnRequest(targetAvatarId, "account-2", "Gate9", 101),
                out AvatarEntity? targetAvatar,
                out string? targetError));
            Assert.Null(targetError);

            byte[] payload = [0x67, 0x89];
            sourceAvatar!.CallProxy(targetAvatar!.Proxy!, OnlineStub.BroadcastOnlineAvatarProxyMsgId, payload);

            Assert.Equal(1, transport.ServerProxyForwardCallCount);
            Assert.Equal(targetAvatar.EntityId, transport.LastServerProxyTargetEntityId);
            Assert.Equal("Gate9", transport.LastServerProxyRouteGateNodeId);
            Assert.Equal(1, transport.ClientProxyForwardCallCount);
            Assert.Equal(targetAvatar.EntityId, transport.LastClientProxyTargetEntityId);
            Assert.Equal("Gate9", transport.LastClientProxyRouteGateNodeId);
            Assert.Collection(
                targetAvatar.ReceivedProxyMessages,
                received =>
                {
                    Assert.Equal(OnlineStub.BroadcastOnlineAvatarProxyMsgId, received.MsgId);
                    Assert.Equal(payload, received.Payload);
                });
        }

        [Fact]
        public void OnlineStub_BroadcastStubCall_FansOutToAllRegisteredAvatarsViaProxy()
        {
            LoopbackServerEntityMessageTransport transport = new();
            GameNodeRuntimeState onlineRuntime = new("Game0", messageTransport: transport);
            GameNodeRuntimeState remoteRuntime = new("Game1", messageTransport: transport);
            transport.Register(onlineRuntime);
            transport.Register(remoteRuntime);

            ServerStubOwnershipSnapshot snapshot = new(
                1,
                [
                    new ServerStubOwnershipAssignment(nameof(OnlineStub), "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, onlineRuntime.ApplyOwnership(snapshot));
            Assert.Equal(GameNodeRuntimeStateErrorCode.None, remoteRuntime.ApplyOwnership(snapshot));

            Assert.True(onlineRuntime.TryCreateAvatarEntity(
                new AvatarEntitySpawnRequest(Guid.NewGuid(), "account-local", "Gate0", 1),
                out AvatarEntity? localAvatar,
                out string? localError));
            Assert.Null(localError);
            Assert.True(remoteRuntime.TryCreateAvatarEntity(
                new AvatarEntitySpawnRequest(Guid.NewGuid(), "account-remote", "Gate9", 2),
                out AvatarEntity? remoteAvatar,
                out string? remoteError));
            Assert.Null(remoteError);

            byte[] payload = [0xCA, 0xFE];
            StubCallErrorCode result = onlineRuntime.ReceiveStubCall(
                nameof(OnlineStub),
                new EntityMessage(OnlineStub.BroadcastOnlineAvatarMessageStubMsgId, payload));

            Assert.Equal(StubCallErrorCode.None, result);
            Assert.Equal(1, transport.ServerProxyForwardCallCount);
            Assert.Equal(2, transport.ClientProxyForwardCallCount);
            Assert.Collection(
                localAvatar!.ReceivedProxyMessages,
                received =>
                {
                    Assert.Equal(OnlineStub.BroadcastOnlineAvatarProxyMsgId, received.MsgId);
                    Assert.Equal(payload, received.Payload);
                });
            Assert.Collection(
                remoteAvatar!.ReceivedProxyMessages,
                received =>
                {
                    Assert.Equal(OnlineStub.BroadcastOnlineAvatarProxyMsgId, received.MsgId);
                    Assert.Equal(payload, received.Payload);
                });
        }
    }
}

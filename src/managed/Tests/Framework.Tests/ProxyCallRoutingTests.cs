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
            LoopbackStubCallTransport stubTransport = new();
            LoopbackProxyCallTransport proxyTransport = new();
            RecordingClientMessageTransport clientTransport = new();
            GameNodeRuntimeState runtimeState = new(
                "Game0",
                stubCallTransport: stubTransport,
                proxyCallTransport: proxyTransport,
                clientMessageTransport: clientTransport);
            stubTransport.Register(runtimeState);
            proxyTransport.Register(runtimeState);

            ServerStubOwnershipSnapshot snapshot = new(
                1,
                [
                    new ServerStubOwnershipAssignment(nameof(OnlineStub), "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(snapshot));

            Guid sourceAvatarId = Guid.NewGuid();
            Guid targetAvatarId = Guid.NewGuid();
            Assert.True(runtimeState.TryCreateAvatarEntity(
                new AvatarEntitySpawnRequest(sourceAvatarId, "account-1", "Hero One", "Gate0", 100),
                out AvatarEntity? sourceAvatar,
                out string? sourceError));
            Assert.Null(sourceError);
            Assert.True(runtimeState.TryCreateAvatarEntity(
                new AvatarEntitySpawnRequest(targetAvatarId, "account-2", "Hero Two", "Gate0", 101),
                out AvatarEntity? targetAvatar,
                out string? targetError));
            Assert.Null(targetError);

            byte[] payload = [0x01, 0x23, 0x45];
            sourceAvatar!.CallProxy(targetAvatar!.Proxy!, OnlineStub.BroadcastOnlineAvatarProxyMsgId, payload);

            Assert.Equal(0, proxyTransport.ForwardCallCount);
            Assert.Equal(1, clientTransport.ForwardCallCount);
            Assert.Equal(targetAvatar.EntityId, clientTransport.LastTargetEntityId);
            Assert.Equal("Gate0", clientTransport.LastRouteGateNodeId);
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
            LoopbackStubCallTransport stubTransport = new();
            LoopbackProxyCallTransport proxyTransport = new();
            RecordingClientMessageTransport clientTransport = new();
            GameNodeRuntimeState onlineRuntime = new("Game9", stubCallTransport: stubTransport, proxyCallTransport: proxyTransport, clientMessageTransport: clientTransport);
            GameNodeRuntimeState sourceRuntime = new("Game0", stubCallTransport: stubTransport, proxyCallTransport: proxyTransport, clientMessageTransport: clientTransport);
            GameNodeRuntimeState targetRuntime = new("Game1", stubCallTransport: stubTransport, proxyCallTransport: proxyTransport, clientMessageTransport: clientTransport);
            stubTransport.Register(onlineRuntime);
            stubTransport.Register(sourceRuntime);
            stubTransport.Register(targetRuntime);
            proxyTransport.Register(onlineRuntime);
            proxyTransport.Register(sourceRuntime);
            proxyTransport.Register(targetRuntime);

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
                new AvatarEntitySpawnRequest(sourceAvatarId, "account-1", "Hero One", "Gate0", 100),
                out AvatarEntity? sourceAvatar,
                out string? sourceError));
            Assert.Null(sourceError);
            Assert.True(targetRuntime.TryCreateAvatarEntity(
                new AvatarEntitySpawnRequest(targetAvatarId, "account-2", "Hero Two", "Gate9", 101),
                out AvatarEntity? targetAvatar,
                out string? targetError));
            Assert.Null(targetError);

            byte[] payload = [0x67, 0x89];
            sourceAvatar!.CallProxy(targetAvatar!.Proxy!, OnlineStub.BroadcastOnlineAvatarProxyMsgId, payload);

            Assert.Equal(1, proxyTransport.ForwardCallCount);
            Assert.Equal(targetAvatar.EntityId, proxyTransport.LastTargetEntityId);
            Assert.Equal("Gate9", proxyTransport.LastRouteGateNodeId);
            Assert.Equal(1, clientTransport.ForwardCallCount);
            Assert.Equal(targetAvatar.EntityId, clientTransport.LastTargetEntityId);
            Assert.Equal("Gate9", clientTransport.LastRouteGateNodeId);
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
            LoopbackStubCallTransport stubTransport = new();
            LoopbackProxyCallTransport proxyTransport = new();
            RecordingClientMessageTransport clientTransport = new();
            GameNodeRuntimeState onlineRuntime = new("Game0", stubCallTransport: stubTransport, proxyCallTransport: proxyTransport, clientMessageTransport: clientTransport);
            GameNodeRuntimeState remoteRuntime = new("Game1", stubCallTransport: stubTransport, proxyCallTransport: proxyTransport, clientMessageTransport: clientTransport);
            stubTransport.Register(onlineRuntime);
            stubTransport.Register(remoteRuntime);
            proxyTransport.Register(onlineRuntime);
            proxyTransport.Register(remoteRuntime);

            ServerStubOwnershipSnapshot snapshot = new(
                1,
                [
                    new ServerStubOwnershipAssignment(nameof(OnlineStub), "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, onlineRuntime.ApplyOwnership(snapshot));
            Assert.Equal(GameNodeRuntimeStateErrorCode.None, remoteRuntime.ApplyOwnership(snapshot));

            Assert.True(onlineRuntime.TryCreateAvatarEntity(
                new AvatarEntitySpawnRequest(Guid.NewGuid(), "account-local", "Local Hero", "Gate0", 1),
                out AvatarEntity? localAvatar,
                out string? localError));
            Assert.Null(localError);
            Assert.True(remoteRuntime.TryCreateAvatarEntity(
                new AvatarEntitySpawnRequest(Guid.NewGuid(), "account-remote", "Remote Hero", "Gate9", 2),
                out AvatarEntity? remoteAvatar,
                out string? remoteError));
            Assert.Null(remoteError);

            byte[] payload = [0xCA, 0xFE];
            StubCallErrorCode result = onlineRuntime.ReceiveStubCall(
                nameof(OnlineStub),
                new StubCallMessage(OnlineStub.BroadcastOnlineAvatarMessageStubMsgId, payload));

            Assert.Equal(StubCallErrorCode.None, result);
            Assert.Equal(1, proxyTransport.ForwardCallCount);
            Assert.Equal(2, clientTransport.ForwardCallCount);
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

    internal sealed class LoopbackProxyCallTransport : IProxyCallTransport
    {
        private readonly Dictionary<string, GameNodeRuntimeState> _runtimes = new(StringComparer.Ordinal);

        public int ForwardCallCount { get; private set; }

        public Guid LastTargetEntityId { get; private set; }

        public string? LastRouteGateNodeId { get; private set; }

        public ProxyCallMessage? LastMessage { get; private set; }

        public void Register(GameNodeRuntimeState runtimeState)
        {
            _runtimes[runtimeState.NodeId] = runtimeState;
        }

        public ProxyCallErrorCode Forward(
            ProxyAddress targetAddress,
            ProxyCallMessage message)
        {
            ForwardCallCount++;
            LastTargetEntityId = targetAddress.EntityId;
            LastRouteGateNodeId = targetAddress.RouteGateNodeId;
            LastMessage = message;

            foreach (GameNodeRuntimeState runtimeState in _runtimes.Values)
            {
                if (runtimeState.EntityManager.Contains(targetAddress.EntityId))
                {
                    return runtimeState.ReceiveProxyCall(targetAddress.EntityId, message);
                }
            }

            return ProxyCallErrorCode.UnknownTargetEntity;
        }
    }

    internal sealed class RecordingClientMessageTransport : IProxyCallTransport
    {
        public int ForwardCallCount { get; private set; }

        public Guid LastTargetEntityId { get; private set; }

        public string? LastRouteGateNodeId { get; private set; }

        public ProxyCallMessage? LastMessage { get; private set; }

        public ProxyCallErrorCode Forward(
            ProxyAddress targetAddress,
            ProxyCallMessage message)
        {
            ForwardCallCount++;
            LastTargetEntityId = targetAddress.EntityId;
            LastRouteGateNodeId = targetAddress.RouteGateNodeId;
            LastMessage = message;
            return ProxyCallErrorCode.None;
        }
    }
}

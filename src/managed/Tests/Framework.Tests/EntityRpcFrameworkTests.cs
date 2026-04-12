using XServer.Managed.Framework.Entities;
using XServer.Managed.Framework.Rpc;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Tests
{
    public sealed class EntityRpcFrameworkTests
    {
        [Fact]
        public void ReceiveProxyCall_DispatchesServerRpcAndPushesClientRpc()
        {
            LoopbackServerEntityMessageTransport transport = new();
            GameNodeRuntimeState runtimeState = new("Game0", messageTransport: transport);
            transport.Register(runtimeState);

            ServerStubOwnershipSnapshot snapshot = new(
                1,
                [
                    new ServerStubOwnershipAssignment(nameof(OnlineStub), "unknown", "Game0"),
                ]);
            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(snapshot));

            Assert.True(runtimeState.TryCreateAvatarEntity(
                new AvatarEntitySpawnRequest(Guid.NewGuid(), "demo-account", "RpcHero", "Gate0", 42),
                out AvatarEntity? avatar,
                out string? error));
            Assert.Null(error);

            byte[] payload = EntityRpcJsonCodec.Encode(avatar!.EntityId, "SetWeapon", "gun");

            ProxyCallErrorCode result = runtimeState.ReceiveProxyCall(
                avatar.EntityId,
                new EntityMessage(EntityRpcMessageIds.ClientToServerEntityRpcMsgId, payload));

            Assert.Equal(ProxyCallErrorCode.None, result);
            Assert.Equal("gun", avatar.EquippedWeaponId);
            Assert.Equal(1, transport.ClientProxyForwardCallCount);
            Assert.Equal(avatar.EntityId, transport.LastClientProxyTargetEntityId);
            Assert.Equal("Gate0", transport.LastClientProxyRouteGateNodeId);
            Assert.NotNull(transport.LastClientProxyMessage);
            Assert.Equal(EntityRpcMessageIds.ServerToClientEntityRpcMsgId, transport.LastClientProxyMessage!.Value.MsgId);
            Assert.True(EntityRpcJsonCodec.TryDecode(
                transport.LastClientProxyMessage.Value.Payload,
                out EntityRpcInvocationEnvelope envelope,
                out EntityRpcDispatchErrorCode errorCode,
                out string errorMessage));
            Assert.Equal(EntityRpcDispatchErrorCode.None, errorCode);
            Assert.Equal(string.Empty, errorMessage);
            Assert.Equal(avatar.EntityId, envelope.EntityId);
            Assert.Equal("OnSetWeaponResult", envelope.RpcName);
            Assert.True(EntityRpcJsonCodec.TryBindArguments(
                envelope,
                [typeof(bool)],
                out object?[] arguments,
                out errorCode,
                out errorMessage));
            Assert.Equal(EntityRpcDispatchErrorCode.None, errorCode);
            Assert.True(Assert.IsType<bool>(Assert.Single(arguments)));
        }

        [Fact]
        public void ReceiveProxyCall_RejectsUnknownServerRpc()
        {
            LoopbackServerEntityMessageTransport transport = new();
            GameNodeRuntimeState runtimeState = new("Game0", messageTransport: transport);
            transport.Register(runtimeState);

            ServerStubOwnershipSnapshot snapshot = new(
                1,
                [
                    new ServerStubOwnershipAssignment(nameof(OnlineStub), "unknown", "Game0"),
                ]);
            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(snapshot));

            Assert.True(runtimeState.TryCreateAvatarEntity(
                new AvatarEntitySpawnRequest(Guid.NewGuid(), "demo-account", "RpcHero", "Gate0", 42),
                out AvatarEntity? avatar,
                out string? error));
            Assert.Null(error);

            byte[] payload = EntityRpcJsonCodec.Encode(avatar!.EntityId, "MissingRpc", "gun");

            ProxyCallErrorCode result = runtimeState.ReceiveProxyCall(
                avatar.EntityId,
                new EntityMessage(EntityRpcMessageIds.ClientToServerEntityRpcMsgId, payload));

            Assert.Equal(ProxyCallErrorCode.EntityRejected, result);
            Assert.Equal(0, transport.ClientProxyForwardCallCount);
            Assert.Equal(string.Empty, avatar.EquippedWeaponId);
        }
    }
}

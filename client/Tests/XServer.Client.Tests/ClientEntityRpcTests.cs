using XServer.Client.Configuration;
using XServer.Client.Entities;
using XServer.Client.GameLogic;
using XServer.Client.Rpc;
using XServer.Client.Runtime;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.Tests;

public sealed class ClientEntityRpcTests
{
    [Fact]
    public void CallServerRpc_UsesConfiguredSenderAndJsonPayload()
    {
        ClientRuntimeState state = new();
        CapturingClientEntityRpcSender sender = new();
        state.ConfigureRpcSender(sender);
        state.StoreLoginGrant("demo-account", CreateProfile(), DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));

        AvatarEntity avatar = state.SelectAvatar();
        avatar.CallServerRPC("SetWeapon", "gun");

        ClientEntityRpcRequest request = Assert.Single(sender.Requests);
        Assert.Same(avatar, sender.LastSourceEntity);
        Assert.Equal(EntityRpcMessageIds.ClientToServerEntityRpcMsgId, request.MsgId);
        Assert.Equal(avatar.EntityId, request.EntityId);
        Assert.Equal("SetWeapon", request.RpcName);
        Assert.True(EntityRpcJsonCodec.TryDecode(
            request.Payload,
            out EntityRpcInvocationEnvelope envelope,
            out EntityRpcDispatchErrorCode errorCode,
            out string errorMessage));
        Assert.Equal(EntityRpcDispatchErrorCode.None, errorCode);
        Assert.Equal(string.Empty, errorMessage);
        Assert.Equal(avatar.EntityId, envelope.EntityId);
        Assert.Equal("SetWeapon", envelope.RpcName);
        Assert.True(EntityRpcJsonCodec.TryBindArguments(
            envelope,
            [typeof(string)],
            out object?[] arguments,
            out errorCode,
            out errorMessage));
        Assert.Equal(EntityRpcDispatchErrorCode.None, errorCode);
        Assert.Equal("gun", Assert.Single(arguments));
    }

    [Fact]
    public void TryHandleControlPacket_DeliversServerRpcToTargetAvatar()
    {
        ClientRuntimeState state = new();
        state.StoreLoginGrant("demo-account", CreateProfile(), DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));
        AvatarEntity avatar = state.SelectAvatar();

        byte[] payload = EntityRpcJsonCodec.Encode(avatar.EntityId, "OnSetWeaponResult", true);
        PacketView packet = new(
            PacketCodec.CreateHeader(
                EntityRpcMessageIds.ServerToClientEntityRpcMsgId,
                3U,
                PacketFlags.None,
                checked((uint)payload.Length)),
            payload);

        ClientGameLogicService service = new();
        string? message = service.TryHandleControlPacket(state, packet);

        Assert.Equal($"clientRpc delivered entityId={avatar.EntityId:D} rpc=OnSetWeaponResult", message);
        Assert.True(avatar.LastSetWeaponSucceeded);
    }

    private static ResolvedClientProfile CreateProfile(
        uint conversation = 321U,
        string endpointSource = "config")
    {
        return new ResolvedClientProfile(
            "configs/local-dev.json",
            "Gate0",
            "127.0.0.1",
            4000,
            conversation,
            new KcpTransportOptions(1200, 256, 128, true, 10, 2, false, 30, 20, false),
            "127.0.0.1",
            4100,
            endpointSource);
    }

    private sealed class CapturingClientEntityRpcSender : IClientEntityRpcSender
    {
        public List<ClientEntityRpcRequest> Requests { get; } = [];

        public ClientEntity? LastSourceEntity { get; private set; }

        public void SendServerRpc(ClientEntity sourceEntity, ClientEntityRpcRequest request)
        {
            LastSourceEntity = sourceEntity;
            Requests.Add(request);
        }
    }
}

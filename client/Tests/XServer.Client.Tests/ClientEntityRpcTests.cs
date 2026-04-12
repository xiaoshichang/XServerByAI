using XServer.Client.Configuration;
using XServer.Client.Entities;
using XServer.Client.Protocol;
using XServer.Client.Rpc;
using XServer.Client.Runtime;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.Tests;

public sealed class ClientEntityRpcTests
{
    [Fact]
    public void CallServerRpc_UsesConfiguredSenderAndJsonPayload()
    {
        GameInstance gameInstance = new();
        CapturingClientEntityRpcSender sender = new();
        gameInstance.ConfigureRpcSender(sender);
        gameInstance.StoreLoginGrant("demo-account", CreateProfile(), DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));

        AvatarEntity avatar = gameInstance.SelectAvatar();
        avatar.CallServerRPC("SetWeapon", "gun");

        ClientEntityRpcRequest request = Assert.Single(sender.Requests);
        Assert.Same(avatar, sender.LastSourceEntity);
        Assert.Equal(ClientMessageIds.ClientToServerEntityRpc, request.MsgId);
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
    public void CallServerRpc_WithPacketSender_WrapsPayloadIntoClientPacketAndRecordsSend()
    {
        GameInstance gameInstance = new();
        PacketHeader sentHeader = default;
        ReadOnlyMemory<byte> sentPayload = ReadOnlyMemory<byte>.Empty;
        ClientEntityRpcPacketSender sender = new(
            gameInstance,
            (header, payload) =>
            {
                sentHeader = header;
                sentPayload = payload;
            });

        gameInstance.ConfigureRpcSender(sender);
        gameInstance.StoreLoginGrant("demo-account", CreateProfile(), DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));

        AvatarEntity avatar = gameInstance.SelectAvatar();
        avatar.CallServerRPC("SetWeapon", "gun");

        Assert.Equal(ClientMessageIds.ClientToServerEntityRpc, sentHeader.MsgId);
        Assert.Equal(PacketFlags.None, sentHeader.Flags);
        Assert.Equal(1U, sentHeader.Seq);
        Assert.Equal((uint)sentPayload.Length, sentHeader.Length);
        Assert.Equal(1, gameInstance.SentPacketCount);
        Assert.Equal(2U, gameInstance.NextPacketSequence);
        Assert.True(EntityRpcJsonCodec.TryDecode(
            sentPayload.ToArray(),
            out EntityRpcInvocationEnvelope envelope,
            out EntityRpcDispatchErrorCode errorCode,
            out string errorMessage));
        Assert.Equal(EntityRpcDispatchErrorCode.None, errorCode);
        Assert.Equal(string.Empty, errorMessage);
        Assert.Equal(avatar.EntityId, envelope.EntityId);
        Assert.Equal("SetWeapon", envelope.RpcName);
    }

    [Fact]
    public void TryHandleClientNetworkPacket_DeliversServerRpcToTargetAvatar()
    {
        GameInstance gameInstance = new();
        gameInstance.StoreLoginGrant("demo-account", CreateProfile(), DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));
        List<string> messages = [];
        gameInstance.ClientNetworkMessageGenerated += messages.Add;
        AvatarEntity avatar = gameInstance.SelectAvatar();

        byte[] payload = EntityRpcJsonCodec.Encode(avatar.EntityId, "OnSetWeaponResult", "gun", true);
        PacketView packet = new(
            PacketCodec.CreateHeader(
                ClientMessageIds.ServerToClientEntityRpc,
                3U,
                PacketFlags.None,
                checked((uint)payload.Length)),
            payload);

        gameInstance.TryHandleClientNetworkPacket(packet);

        string message = Assert.Single(messages);
        Assert.Equal($"clientRpc delivered entityId={avatar.EntityId:D} rpc=OnSetWeaponResult", message);
        Assert.Equal("gun", avatar.Weapon);
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

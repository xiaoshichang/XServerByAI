using System.Text.Json;
using XServer.Client.Configuration;
using XServer.Client.Entities;
using XServer.Client.Rpc;
using XServer.Client.Runtime;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.Tests;

public sealed class GameInstanceGameplayTests
{
    [Fact]
    public void PrepareSelectAvatarRequestDefersLocalSelectionUntilSendCompletes()
    {
        GameInstance gameInstance = new();
        gameInstance.StoreLoginGrant("demo-account", CreateProfile(), DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));

        OutboundGameRequest request = gameInstance.PrepareSelectAvatarRequest();

        Assert.False(gameInstance.HasAvatar);
        Assert.Equal(GameInstance.DefaultSelectAvatarMsgId, request.Header.MsgId);

        request.ApplyAfterSend?.Invoke();

        Assert.True(gameInstance.HasAvatar);
        Assert.False(gameInstance.HasConfirmedAvatar);
        Assert.Equal(1, gameInstance.EntityManager.Count);
        Assert.Equal(ClientLifecycleState.AvatarSelecting, gameInstance.LifecycleState);
    }

    [Fact]
    public void TryHandleControlPacketConfirmsPendingAvatarSelection()
    {
        GameInstance gameInstance = new();
        gameInstance.StoreLoginGrant("demo-account", CreateProfile(), DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));

        OutboundGameRequest request = gameInstance.PrepareSelectAvatarRequest();
        request.ApplyAfterSend?.Invoke();

        AvatarEntity avatar = gameInstance.Avatar!;
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(
            new
            {
                action = "selectAvatarResult",
                success = true,
                accountId = avatar.AccountId,
                avatarId = avatar.AvatarId,
                gameNodeId = "Game0",
                sessionId = 7UL,
            });

        PacketView packet = new(
            PacketCodec.CreateHeader(
                GameInstance.DefaultSelectAvatarMsgId,
                2U,
                PacketFlags.Response,
                checked((uint)payload.Length)),
            payload);

        string? message = gameInstance.TryHandleControlPacket(packet);

        Assert.NotNull(message);
        Assert.Contains("selectAvatar confirmed", message, StringComparison.Ordinal);
        Assert.True(gameInstance.HasConfirmedAvatar);
        Assert.Equal("Game0", gameInstance.AvatarSession.GameNodeId);
        Assert.Equal(7UL, gameInstance.AvatarSession.SessionId);
        Assert.Equal(ClientLifecycleState.AvatarReady, gameInstance.LifecycleState);
    }

    [Fact]
    public void PrepareMoveRequestAppliesLocalPositionWhenRequested()
    {
        GameInstance gameInstance = CreateReadyAvatarState();
        OutboundGameRequest request = gameInstance.PrepareMoveRequest(x: 1.5f, y: 2.5f, z: -3.0f, localApply: true);

        request.ApplyAfterSend?.Invoke();

        Assert.Equal(1.5f, gameInstance.Avatar!.PositionX);
        Assert.Equal(2.5f, gameInstance.Avatar.PositionY);
        Assert.Equal(-3.0f, gameInstance.Avatar.PositionZ);
    }

    [Fact]
    public void SendSetWeaponRpc_UsesAvatarEntityRpcSurface()
    {
        GameInstance gameInstance = CreateReadyAvatarState();
        CapturingClientEntityRpcSender sender = new();
        gameInstance.ConfigureRpcSender(sender);

        string summary = gameInstance.SendSetWeaponRpc("gun");

        ClientEntityRpcRequest request = Assert.Single(sender.Requests);
        Assert.Contains("set-weapon rpc sent msgId=6302", summary, StringComparison.Ordinal);
        Assert.Equal(EntityRpcMessageIds.ClientToServerEntityRpcMsgId, request.MsgId);
        Assert.Equal(gameInstance.Avatar!.EntityId, request.EntityId);
        Assert.Equal("SetWeapon", request.RpcName);
    }

    [Fact]
    public void TryHandleControlPacketFormatsBoardcaseBroadcast()
    {
        GameInstance gameInstance = new();
        byte[] payload = "hello"u8.ToArray();

        PacketView packet = new(
            PacketCodec.CreateHeader(
                GameInstance.BroadcastMessageMsgId,
                0U,
                PacketFlags.None,
                checked((uint)payload.Length)),
            payload);

        string? message = gameInstance.TryHandleControlPacket(packet);

        Assert.Equal("boardcase received: hello", message);
    }

    private static GameInstance CreateReadyAvatarState()
    {
        GameInstance gameInstance = new();
        gameInstance.StoreLoginGrant("demo-account", CreateProfile(), DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));
        AvatarEntity avatar = gameInstance.SelectAvatar();
        Assert.True(gameInstance.ConfirmAvatarSelection("demo-account", avatar.AvatarId));
        return gameInstance;
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

        public void SendServerRpc(ClientEntity sourceEntity, ClientEntityRpcRequest request)
        {
            _ = sourceEntity;
            Requests.Add(request);
        }
    }
}

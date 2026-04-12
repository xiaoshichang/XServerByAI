using System.Text.Json;
using XServer.Client.Configuration;
using XServer.Client.Entities;
using XServer.Client.Rpc;
using XServer.Client.Runtime;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.Tests;

public sealed class ClientRuntimeStateGameplayTests
{
    [Fact]
    public void PrepareSelectAvatarRequestDefersLocalSelectionUntilSendCompletes()
    {
        ClientRuntimeState state = new();
        state.StoreLoginGrant("demo-account", CreateProfile(), DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));

        OutboundGameRequest request = state.PrepareSelectAvatarRequest();

        Assert.False(state.HasAvatar);
        Assert.Equal(ClientRuntimeState.DefaultSelectAvatarMsgId, request.Header.MsgId);

        request.ApplyAfterSend?.Invoke();

        Assert.True(state.HasAvatar);
        Assert.False(state.HasConfirmedAvatar);
        Assert.Equal(1, state.EntityManager.Count);
        Assert.Equal(ClientLifecycleState.AvatarSelecting, state.LifecycleState);
    }

    [Fact]
    public void TryHandleControlPacketConfirmsPendingAvatarSelection()
    {
        ClientRuntimeState state = new();
        state.StoreLoginGrant("demo-account", CreateProfile(), DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));

        OutboundGameRequest request = state.PrepareSelectAvatarRequest();
        request.ApplyAfterSend?.Invoke();

        AvatarEntity avatar = state.Avatar!;
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
                ClientRuntimeState.DefaultSelectAvatarMsgId,
                2U,
                PacketFlags.Response,
                checked((uint)payload.Length)),
            payload);

        string? message = state.TryHandleControlPacket(packet);

        Assert.NotNull(message);
        Assert.Contains("selectAvatar confirmed", message, StringComparison.Ordinal);
        Assert.True(state.HasConfirmedAvatar);
        Assert.Equal("Game0", state.AvatarSession.GameNodeId);
        Assert.Equal(7UL, state.AvatarSession.SessionId);
        Assert.Equal(ClientLifecycleState.AvatarReady, state.LifecycleState);
    }

    [Fact]
    public void PrepareMoveRequestAppliesLocalPositionWhenRequested()
    {
        ClientRuntimeState state = CreateReadyAvatarState();
        OutboundGameRequest request = state.PrepareMoveRequest(x: 1.5f, y: 2.5f, z: -3.0f, localApply: true);

        request.ApplyAfterSend?.Invoke();

        Assert.Equal(1.5f, state.Avatar!.PositionX);
        Assert.Equal(2.5f, state.Avatar.PositionY);
        Assert.Equal(-3.0f, state.Avatar.PositionZ);
    }

    [Fact]
    public void SendSetWeaponRpc_UsesAvatarEntityRpcSurface()
    {
        ClientRuntimeState state = CreateReadyAvatarState();
        CapturingClientEntityRpcSender sender = new();
        state.ConfigureRpcSender(sender);

        string summary = state.SendSetWeaponRpc("gun");

        ClientEntityRpcRequest request = Assert.Single(sender.Requests);
        Assert.Contains("set-weapon rpc sent msgId=6302", summary, StringComparison.Ordinal);
        Assert.Equal(EntityRpcMessageIds.ClientToServerEntityRpcMsgId, request.MsgId);
        Assert.Equal(state.Avatar!.EntityId, request.EntityId);
        Assert.Equal("SetWeapon", request.RpcName);
    }

    [Fact]
    public void TryHandleControlPacketFormatsBoardcaseBroadcast()
    {
        ClientRuntimeState state = new();
        byte[] payload = "hello"u8.ToArray();

        PacketView packet = new(
            PacketCodec.CreateHeader(
                ClientRuntimeState.BroadcastMessageMsgId,
                0U,
                PacketFlags.None,
                checked((uint)payload.Length)),
            payload);

        string? message = state.TryHandleControlPacket(packet);

        Assert.Equal("boardcase received: hello", message);
    }

    private static ClientRuntimeState CreateReadyAvatarState()
    {
        ClientRuntimeState state = new();
        state.StoreLoginGrant("demo-account", CreateProfile(), DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));
        AvatarEntity avatar = state.SelectAvatar();
        Assert.True(state.ConfirmAvatarSelection("demo-account", avatar.AvatarId));
        return state;
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

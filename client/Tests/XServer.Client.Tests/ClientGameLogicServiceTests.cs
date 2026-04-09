using System.Text.Json;
using XServer.Client.Configuration;
using XServer.Client.GameLogic;
using XServer.Client.Runtime;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.Tests;

public sealed class ClientGameLogicServiceTests
{
    [Fact]
    public void PrepareSelectAvatarRequestDefersLocalSelectionUntilSendCompletes()
    {
        ClientRuntimeState state = new();
        state.StoreLoginGrant("demo-account", CreateProfile(), DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));

        ClientGameLogicService service = new();
        OutboundGameRequest request = service.PrepareSelectAvatarRequest(state);

        Assert.False(state.HasAvatar);
        Assert.Equal(ClientGameLogicService.DefaultSelectAvatarMsgId, request.Header.MsgId);

        request.ApplyAfterSend?.Invoke();

        Assert.True(state.HasAvatar);
        Assert.False(state.HasConfirmedAvatar);
        Assert.Equal(ClientLifecycleState.AvatarSelecting, state.LifecycleState);
    }

    [Fact]
    public void TryHandleControlPacketConfirmsPendingAvatarSelection()
    {
        ClientRuntimeState state = new();
        state.StoreLoginGrant("demo-account", CreateProfile(), DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));

        ClientGameLogicService service = new();
        OutboundGameRequest request = service.PrepareSelectAvatarRequest(state);
        request.ApplyAfterSend?.Invoke();

        AvatarView avatar = state.Avatar!;
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(
            new
            {
                action = "selectAvatarResult",
                success = true,
                accountId = avatar.AccountId,
                avatarId = avatar.AvatarId,
                avatarName = avatar.DisplayName,
                gameNodeId = "Game0",
                sessionId = 7UL,
            });

        PacketView packet = new(
            PacketCodec.CreateHeader(
                ClientGameLogicService.DefaultSelectAvatarMsgId,
                2U,
                PacketFlags.Response,
                checked((uint)payload.Length)),
            payload);

        string? message = service.TryHandleControlPacket(state, packet);

        Assert.NotNull(message);
        Assert.Contains("selectAvatar confirmed", message, StringComparison.Ordinal);
        Assert.True(state.HasConfirmedAvatar);
        Assert.Equal(ClientLifecycleState.AvatarReady, state.LifecycleState);
    }

    [Fact]
    public void PrepareMoveRequestAppliesLocalPositionWhenRequested()
    {
        ClientRuntimeState state = CreateReadyAvatarState();
        ClientGameLogicService service = new();

        OutboundGameRequest request = service.PrepareMoveRequest(state, x: 1.5f, y: 2.5f, z: -3.0f, localApply: true);

        request.ApplyAfterSend?.Invoke();

        Assert.Equal(1.5f, state.Avatar!.PositionX);
        Assert.Equal(2.5f, state.Avatar.PositionY);
        Assert.Equal(-3.0f, state.Avatar.PositionZ);
    }

    private static ClientRuntimeState CreateReadyAvatarState()
    {
        ClientRuntimeState state = new();
        state.StoreLoginGrant("demo-account", CreateProfile(), DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));
        AvatarView avatar = state.SelectAvatar();
        Assert.True(state.ConfirmAvatarSelection("demo-account", avatar.AvatarId, avatar.DisplayName));
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
}

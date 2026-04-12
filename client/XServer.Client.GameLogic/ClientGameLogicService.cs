using System.Text;
using System.Text.Json;
using XServer.Client.Entities;
using XServer.Client.Rpc;
using XServer.Client.Runtime;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.GameLogic;

public sealed class ClientGameLogicService
{
    public const uint DefaultMoveMsgId = 45011U;
    public const uint DefaultBuyWeaponMsgId = 45012U;
    public const uint DefaultSelectAvatarMsgId = 45013U;
    public const uint BroadcastMessageMsgId = 6201U;

    private static readonly JsonSerializerOptions ControlJsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
    };

    private sealed class SelectAvatarResponse
    {
        public string? Action { get; init; }
        public bool Success { get; init; }
        public string? AccountId { get; init; }
        public string? AvatarId { get; init; }
        public string? AvatarName { get; init; }
        public string? GameNodeId { get; init; }
        public ulong SessionId { get; init; }
        public string? Error { get; init; }
    }

    public OutboundGameRequest PrepareSelectAvatarRequest(ClientRuntimeState state, uint? msgId = null)
    {
        ArgumentNullException.ThrowIfNull(state);
        EnsureAccountReady(state);

        string accountId = state.Account!.AccountId;
        AvatarEntity selectedAvatar = state.CreateTemporaryAvatarSelection();
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(
            new
            {
                action = "selectAvatar",
                accountId,
                avatarId = selectedAvatar.AvatarId,
                avatarName = selectedAvatar.DisplayName,
            });

        uint effectiveMsgId = msgId ?? DefaultSelectAvatarMsgId;
        PacketHeader header = PacketCodec.CreateHeader(
            effectiveMsgId,
            state.AllocatePacketSequence(),
            PacketFlags.None,
            checked((uint)payload.Length));

        return new OutboundGameRequest(
            header,
            payload,
            $"selectAvatar request sent msgId={effectiveMsgId} account={accountId} avatarId={selectedAvatar.AvatarId} avatarName={selectedAvatar.DisplayName}; waiting for server confirmation.",
            () => state.SelectAvatar(selectedAvatar));
    }

    public OutboundGameRequest PrepareMoveRequest(
        ClientRuntimeState state,
        float? x = null,
        float? y = null,
        float? z = null,
        bool localApply = true,
        uint? msgId = null)
    {
        ArgumentNullException.ThrowIfNull(state);
        EnsureAvatarReady(state);

        float effectiveX = x ?? state.Avatar!.PositionX;
        float effectiveY = y ?? state.Avatar!.PositionY;
        float effectiveZ = z ?? state.Avatar!.PositionZ;
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(
            new
            {
                action = "move",
                avatarId = state.Avatar!.AvatarId,
                position = new { x = effectiveX, y = effectiveY, z = effectiveZ },
            });

        uint effectiveMsgId = msgId ?? DefaultMoveMsgId;
        PacketHeader header = PacketCodec.CreateHeader(
            effectiveMsgId,
            state.AllocatePacketSequence(),
            PacketFlags.None,
            checked((uint)payload.Length));

        Action? applyAfterSend = localApply
            ? () => state.UpdateAvatarPosition(effectiveX, effectiveY, effectiveZ)
            : null;

        return new OutboundGameRequest(
            header,
            payload,
            $"move request sent msgId={effectiveMsgId} pos=({effectiveX}, {effectiveY}, {effectiveZ}) localApply={localApply}",
            applyAfterSend);
    }

    public OutboundGameRequest PrepareBuyWeaponRequest(
        ClientRuntimeState state,
        string weaponId = "training-sword",
        int count = 1,
        bool localApply = true,
        uint? msgId = null)
    {
        ArgumentNullException.ThrowIfNull(state);
        EnsureAvatarReady(state);

        if (string.IsNullOrWhiteSpace(weaponId))
        {
            throw new ArgumentException("weaponId must not be empty.", nameof(weaponId));
        }

        if (count <= 0)
        {
            throw new ArgumentException("count must be greater than zero.", nameof(count));
        }

        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(
            new
            {
                action = "buyWeapon",
                avatarId = state.Avatar!.AvatarId,
                weaponId,
                count,
            });

        uint effectiveMsgId = msgId ?? DefaultBuyWeaponMsgId;
        PacketHeader header = PacketCodec.CreateHeader(
            effectiveMsgId,
            state.AllocatePacketSequence(),
            PacketFlags.None,
            checked((uint)payload.Length));

        Action? applyAfterSend = localApply
            ? () => state.AddWeapon(weaponId, count)
            : null;

        return new OutboundGameRequest(
            header,
            payload,
            $"buyWeapon request sent msgId={effectiveMsgId} weaponId={weaponId} count={count} localApply={localApply}",
            applyAfterSend);
    }

    public string SendSetWeaponRpc(ClientRuntimeState state, string weapon)
    {
        ArgumentNullException.ThrowIfNull(state);
        EnsureAvatarReady(state);

        if (string.IsNullOrWhiteSpace(weapon))
        {
            throw new ArgumentException("weapon must not be empty.", nameof(weapon));
        }

        state.Avatar!.CallServerRpc("SetWeapon", weapon);
        return
            $"set-weapon rpc sent msgId={EntityRpcMessageIds.ClientToServerEntityRpcMsgId} entityId={state.Avatar.AvatarId} weapon={weapon}";
    }

    public string? TryHandleControlPacket(ClientRuntimeState state, PacketView packet)
    {
        ArgumentNullException.ThrowIfNull(state);

        if (state.TryHandleServerRpcPacket(packet, out string? rpcMessage))
        {
            return rpcMessage;
        }

        if (packet.Header.MsgId == BroadcastMessageMsgId)
        {
            return TryHandleBroadcastPacket(packet);
        }

        if (packet.Header.MsgId != DefaultSelectAvatarMsgId ||
            (packet.Header.Flags & PacketFlags.Response) == PacketFlags.None)
        {
            return null;
        }

        SelectAvatarResponse? response;
        try
        {
            response = JsonSerializer.Deserialize<SelectAvatarResponse>(packet.Payload.Span, ControlJsonOptions);
        }
        catch (JsonException)
        {
            return "selectAvatar response arrived but the control payload could not be decoded.";
        }

        if (response is null || !string.Equals(response.Action, "selectAvatarResult", StringComparison.Ordinal))
        {
            return "selectAvatar response arrived but the control payload action was not recognized.";
        }

        if ((packet.Header.Flags & PacketFlags.Error) != PacketFlags.None || !response.Success)
        {
            state.ClearAvatarSelection();
            return
                $"selectAvatar failed account={response.AccountId ?? "<unknown>"} avatarId={response.AvatarId ?? "<unknown>"} error={response.Error ?? "unknown error"}";
        }

        if (string.IsNullOrWhiteSpace(response.AccountId) || string.IsNullOrWhiteSpace(response.AvatarId))
        {
            return "selectAvatar confirmation payload was incomplete.";
        }

        if (!state.ConfirmAvatarSelection(
                response.AccountId,
                response.AvatarId,
                response.AvatarName,
                response.GameNodeId,
                response.SessionId))
        {
            return
                $"selectAvatar confirmation did not match the local pending selection account={response.AccountId} avatarId={response.AvatarId}";
        }

        return
            $"selectAvatar confirmed account={response.AccountId} avatarId={response.AvatarId} avatarName={response.AvatarName ?? response.AvatarId} game={response.GameNodeId ?? "<unknown>"} sessionId={response.SessionId}";
    }

    private static string TryHandleBroadcastPacket(PacketView packet)
    {
        if (packet.Payload.IsEmpty)
        {
            return "boardcase received: <empty>";
        }

        try
        {
            string text = Encoding.UTF8.GetString(packet.Payload.Span);
            return $"boardcase received: {text}";
        }
        catch (DecoderFallbackException)
        {
            return $"boardcase received: payloadBytes={packet.Payload.Length}";
        }
    }

    private static void EnsureAccountReady(ClientRuntimeState state)
    {
        if (!state.HasAccount)
        {
            throw new InvalidOperationException(
                "No local Account is cached yet. Run login <url> <account> <password> first.");
        }
    }

    private static void EnsureAvatarReady(ClientRuntimeState state)
    {
        if (!state.HasAvatar)
        {
            throw new InvalidOperationException(
                "No local Avatar is bound to the current Account. Run login <url> <account> <password>, connect, then selectAvatar before move/buyWeapon/set-weapon.");
        }

        if (!state.HasConfirmedAvatar)
        {
            throw new InvalidOperationException(
                "The selected Avatar is still waiting for server confirmation. Wait for the selectAvatar success response before move/buyWeapon/set-weapon.");
        }
    }
}

public sealed record OutboundGameRequest(
    PacketHeader Header,
    byte[] Payload,
    string Summary,
    Action? ApplyAfterSend = null);

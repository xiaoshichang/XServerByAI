using System.Text;
using System.Text.Json;
using XServer.Client.Entities;
using XServer.Client.Rpc;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.Runtime;

public sealed partial class ClientRuntimeState
{
    public const uint DefaultMoveMsgId = 45011U;
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
        public string? GameNodeId { get; init; }
        public ulong SessionId { get; init; }
        public string? Error { get; init; }
    }

    public OutboundGameRequest PrepareSelectAvatarRequest(uint? msgId = null)
    {
        EnsureAccountReady();

        string accountId = Account!.AccountId;
        AvatarEntity selectedAvatar = CreateTemporaryAvatarSelection();
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(
            new
            {
                action = "selectAvatar",
                accountId,
                avatarId = selectedAvatar.AvatarId,
            });

        uint effectiveMsgId = msgId ?? DefaultSelectAvatarMsgId;
        PacketHeader header = PacketCodec.CreateHeader(
            effectiveMsgId,
            AllocatePacketSequence(),
            PacketFlags.None,
            checked((uint)payload.Length));

        return new OutboundGameRequest(
            header,
            payload,
            $"selectAvatar request sent msgId={effectiveMsgId} account={accountId} avatarId={selectedAvatar.AvatarId}; waiting for server confirmation.",
            () => SelectAvatar(selectedAvatar));
    }

    public OutboundGameRequest PrepareMoveRequest(
        float? x = null,
        float? y = null,
        float? z = null,
        bool localApply = true,
        uint? msgId = null)
    {
        EnsureAvatarReady();

        float effectiveX = x ?? Avatar!.PositionX;
        float effectiveY = y ?? Avatar!.PositionY;
        float effectiveZ = z ?? Avatar!.PositionZ;
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(
            new
            {
                action = "move",
                avatarId = Avatar!.AvatarId,
                position = new { x = effectiveX, y = effectiveY, z = effectiveZ },
            });

        uint effectiveMsgId = msgId ?? DefaultMoveMsgId;
        PacketHeader header = PacketCodec.CreateHeader(
            effectiveMsgId,
            AllocatePacketSequence(),
            PacketFlags.None,
            checked((uint)payload.Length));

        Action? applyAfterSend = localApply
            ? () => UpdateAvatarPosition(effectiveX, effectiveY, effectiveZ)
            : null;

        return new OutboundGameRequest(
            header,
            payload,
            $"move request sent msgId={effectiveMsgId} pos=({effectiveX}, {effectiveY}, {effectiveZ}) localApply={localApply}",
            applyAfterSend);
    }

    public string SendSetWeaponRpc(string weapon)
    {
        EnsureAvatarReady();

        if (string.IsNullOrWhiteSpace(weapon))
        {
            throw new ArgumentException("weapon must not be empty.", nameof(weapon));
        }

        Avatar!.CallServerRpc("SetWeapon", weapon);
        return
            $"set-weapon rpc sent msgId={EntityRpcMessageIds.ClientToServerEntityRpcMsgId} entityId={Avatar.AvatarId} weapon={weapon}";
    }

    public string? TryHandleControlPacket(PacketView packet)
    {
        if (TryHandleServerRpcPacket(packet, out string? rpcMessage))
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
            ClearAvatarSelection();
            return
                $"selectAvatar failed account={response.AccountId ?? "<unknown>"} avatarId={response.AvatarId ?? "<unknown>"} error={response.Error ?? "unknown error"}";
        }

        if (string.IsNullOrWhiteSpace(response.AccountId) || string.IsNullOrWhiteSpace(response.AvatarId))
        {
            return "selectAvatar confirmation payload was incomplete.";
        }

        if (!ConfirmAvatarSelection(
                response.AccountId,
                response.AvatarId,
                response.GameNodeId,
                response.SessionId))
        {
            return
                $"selectAvatar confirmation did not match the local pending selection account={response.AccountId} avatarId={response.AvatarId}";
        }

        return
            $"selectAvatar confirmed account={response.AccountId} avatarId={response.AvatarId} game={response.GameNodeId ?? "<unknown>"} sessionId={response.SessionId}";
    }

    public bool TryHandleServerRpcPacket(PacketView packet, out string? message)
    {
        if (packet.Header.MsgId != EntityRpcMessageIds.ServerToClientEntityRpcMsgId)
        {
            message = null;
            return false;
        }

        EntityRpcDispatchErrorCode result = ClientEntityRpcDispatcher.Dispatch(
            this,
            packet.Payload,
            out Guid entityId,
            out string rpcName,
            out string errorMessage);
        message = result == EntityRpcDispatchErrorCode.None
            ? $"clientRpc delivered entityId={entityId:D} rpc={rpcName}"
            : $"clientRpc failed entityId={entityId:D} rpc={rpcName} error={errorMessage}";
        return true;
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
}

public sealed record OutboundGameRequest(
    PacketHeader Header,
    byte[] Payload,
    string Summary,
    Action? ApplyAfterSend = null);

using System.Text.Json;
using XServer.Client.Entities;
using XServer.Client.Protocol;
using XServer.Client.Rpc;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.Runtime;

public sealed partial class GameInstance
{
    public OutboundGameRequest PrepareSelectAvatarRequest(uint? msgId = null)
    {
        EnsureAccountReady();

        string accountId = Account!.AccountId;
        AvatarEntity selectedAvatar = CreateTemporaryAvatarSelection();
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(
            new ClientServerSelectAvatarPayload
            {
                AccountId = accountId,
                AvatarId = selectedAvatar.AvatarId,
            });

        uint effectiveMsgId = msgId ?? ClientMessageIds.SelectAvatar;
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
            new ClientServerMovePayload
            {
                AvatarId = Avatar!.AvatarId,
                Position = new ClientServerMovePositionPayload
                {
                    X = effectiveX,
                    Y = effectiveY,
                    Z = effectiveZ,
                },
            });

        uint effectiveMsgId = msgId ?? ClientMessageIds.Move;
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
            $"set-weapon rpc sent msgId={ClientMessageIds.ClientToServerEntityRpc} entityId={Avatar.AvatarId} weapon={weapon}";
    }

    public void TryHandleClientNetworkPacket(PacketView packet)
    {
        string? message = (packet.Header.MsgId, (packet.Header.Flags & PacketFlags.Response) != PacketFlags.None) switch
        {
            (ClientMessageIds.ServerToClientEntityRpc, _) => DispatchServerRpcPacket(packet),
            (ClientMessageIds.BroadcastMessage, _) => TryHandleBroadcastPacket(packet),
            (ClientMessageIds.SelectAvatar, true) => TryHandleSelectAvatarResponsePacket(packet),
            _ => null,
        };

        PublishClientNetworkMessage(message);
    }

    public bool TryHandleServerRpcPacket(PacketView packet, out string? message)
    {
        if (packet.Header.MsgId != ClientMessageIds.ServerToClientEntityRpc)
        {
            message = null;
            return false;
        }

        message = DispatchServerRpcPacket(packet);
        return true;
    }

    private string TryHandleSelectAvatarResponsePacket(PacketView packet)
    {
        ServerClientSelectAvatarResultPayload? response;
        try
        {
            response = JsonSerializer.Deserialize<ServerClientSelectAvatarResultPayload>(
                packet.Payload.Span,
                ProtocolJsonOptions.Default);
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

    private string DispatchServerRpcPacket(PacketView packet)
    {
        EntityRpcDispatchErrorCode result = ClientEntityRpcDispatcher.Dispatch(
            this,
            packet.Payload,
            out Guid entityId,
            out string rpcName,
            out string errorMessage);
        return result == EntityRpcDispatchErrorCode.None
            ? $"clientRpc delivered entityId={entityId:D} rpc={rpcName}"
            : $"clientRpc failed entityId={entityId:D} rpc={rpcName} error={errorMessage}";
    }

    private static string TryHandleBroadcastPacket(PacketView packet)
    {
        if (!ServerClientBoardcasePayloadCodec.TryDecode(packet.Payload, out ServerClientBoardcasePayload message))
        {
            return packet.Payload.IsEmpty
                ? "boardcase received: <empty>"
                : $"boardcase received: payloadBytes={packet.Payload.Length}";
        }

        return $"boardcase received: {message.Text}";
    }
}

public sealed record OutboundGameRequest(
    PacketHeader Header,
    byte[] Payload,
    string Summary,
    Action? ApplyAfterSend = null);

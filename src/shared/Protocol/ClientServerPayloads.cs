using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

#if XSERVER_CLIENT_FRAMEWORK
using XServer.Client.Rpc;

namespace XServer.Client.Protocol;
#elif XSERVER_SERVER_FRAMEWORK
using XServer.Managed.Framework.Rpc;

namespace XServer.Managed.Framework.Protocol;
#else
#error Shared protocol sources must be compiled by a framework project.
#endif

public readonly record struct ClientServerClientHelloPayload;

public sealed class ClientServerMovePositionPayload
{
    [JsonPropertyName("x")]
    public float X { get; init; }

    [JsonPropertyName("y")]
    public float Y { get; init; }

    [JsonPropertyName("z")]
    public float Z { get; init; }
}

public sealed class ClientServerMovePayload
{
    [JsonPropertyName("action")]
    public string Action { get; init; } = "move";

    [JsonPropertyName("avatarId")]
    public string? AvatarId { get; init; }

    [JsonPropertyName("position")]
    public ClientServerMovePositionPayload? Position { get; init; }
}

public sealed class ClientServerSelectAvatarPayload
{
    [JsonPropertyName("action")]
    public string Action { get; init; } = "selectAvatar";

    [JsonPropertyName("accountId")]
    public string? AccountId { get; init; }

    [JsonPropertyName("avatarId")]
    public string? AvatarId { get; init; }
}

public sealed class ServerClientSelectAvatarResultPayload
{
    [JsonPropertyName("action")]
    public string Action { get; init; } = "selectAvatarResult";

    [JsonPropertyName("success")]
    public bool Success { get; init; }

    [JsonPropertyName("accountId")]
    public string? AccountId { get; init; }

    [JsonPropertyName("avatarId")]
    public string? AvatarId { get; init; }

    [JsonPropertyName("gameNodeId")]
    public string? GameNodeId { get; init; }

    [JsonPropertyName("sessionId")]
    public ulong SessionId { get; init; }

    [JsonPropertyName("error")]
    public string? Error { get; init; }
}

public readonly record struct ServerClientBoardcasePayload(string Text);

public static class ServerClientBoardcasePayloadCodec
{
    private static readonly Encoding StrictUtf8 = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false, throwOnInvalidBytes: true);

    public static byte[] Encode(ServerClientBoardcasePayload payload)
    {
        return StrictUtf8.GetBytes(payload.Text ?? string.Empty);
    }

    public static bool TryDecode(ReadOnlyMemory<byte> payload, out ServerClientBoardcasePayload message)
    {
        try
        {
            message = new ServerClientBoardcasePayload(StrictUtf8.GetString(payload.Span));
            return true;
        }
        catch (DecoderFallbackException)
        {
            message = default;
            return false;
        }
    }
}

public readonly record struct ClientServerEntityRpcPayload(
    Guid EntityId,
    string RpcName,
    IReadOnlyList<JsonElement> Arguments)
{
    public EntityRpcInvocationEnvelope ToInvocationEnvelope()
    {
        return new EntityRpcInvocationEnvelope(EntityId, RpcName, Arguments);
    }

    public static ClientServerEntityRpcPayload FromInvocationEnvelope(EntityRpcInvocationEnvelope envelope)
    {
        return new ClientServerEntityRpcPayload(envelope.EntityId, envelope.RpcName, envelope.Arguments);
    }
}

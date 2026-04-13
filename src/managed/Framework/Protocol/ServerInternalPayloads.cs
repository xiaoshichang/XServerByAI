using System.Text.Json.Serialization;

namespace XServer.Managed.Framework.Protocol;

public enum ServerInternalProcessType : ushort
{
    Gate = 1,
    Game = 2,
}

public sealed class ServerInternalEndpointPayload
{
    public string Host { get; init; } = string.Empty;

    public ushort Port { get; init; }
}

public sealed class ServerLoadSnapshotPayload
{
    public uint ConnectionCount { get; init; }

    public uint SessionCount { get; init; }

    public uint EntityCount { get; init; }

    public uint SpaceCount { get; init; }

    public uint LoadScore { get; init; }
}

public sealed class ServerRegisterRequestPayload
{
    public ServerInternalProcessType ProcessType { get; init; }

    public ushort ProcessFlags { get; init; }

    public string NodeId { get; init; } = string.Empty;

    public uint Pid { get; init; }

    public ulong StartedAtUnixMs { get; init; }

    public ServerInternalEndpointPayload InnerNetworkEndpoint { get; init; } = new();

    public string BuildVersion { get; init; } = string.Empty;

    public IReadOnlyList<string> CapabilityTags { get; init; } = Array.Empty<string>();

    public ServerLoadSnapshotPayload Load { get; init; } = new();
}

public sealed class ServerRegisterSuccessResponsePayload
{
    public uint HeartbeatIntervalMs { get; init; }

    public uint HeartbeatTimeoutMs { get; init; }

    public ulong ServerNowUnixMs { get; init; }
}

public sealed class ServerRegisterErrorResponsePayload
{
    public int ErrorCode { get; init; }

    public uint RetryAfterMs { get; init; }
}

public sealed class ServerHeartbeatRequestPayload
{
    public ulong SentAtUnixMs { get; init; }

    public uint StatusFlags { get; init; }

    public ServerLoadSnapshotPayload Load { get; init; } = new();
}

public sealed class ServerHeartbeatSuccessResponsePayload
{
    public uint HeartbeatIntervalMs { get; init; }

    public uint HeartbeatTimeoutMs { get; init; }

    public ulong ServerNowUnixMs { get; init; }
}

public sealed class ClusterNodesOnlineNotifyPayload
{
    public bool AllNodesOnline { get; init; }

    public uint StatusFlags { get; init; }

    public ulong ServerNowUnixMs { get; init; }
}

public sealed class GameGateMeshReadyReportPayload
{
    public uint StatusFlags { get; init; }

    public ulong ReportedAtUnixMs { get; init; }
}

public sealed class ServerStubOwnershipEntryPayload
{
    public string EntityType { get; init; } = string.Empty;

    public string EntityId { get; init; } = string.Empty;

    public string OwnerGameNodeId { get; init; } = string.Empty;

    public uint EntryFlags { get; init; }
}

public sealed class ServerStubOwnershipSyncPayload
{
    public ulong AssignmentEpoch { get; init; }

    public uint StatusFlags { get; init; }

    public IReadOnlyList<ServerStubOwnershipEntryPayload> Assignments { get; init; } =
        Array.Empty<ServerStubOwnershipEntryPayload>();

    public ulong ServerNowUnixMs { get; init; }
}

public sealed class ServerStubReadyEntryPayload
{
    public string EntityType { get; init; } = string.Empty;

    public string EntityId { get; init; } = string.Empty;

    public bool Ready { get; init; }

    public uint EntryFlags { get; init; }
}

public sealed class GameServiceReadyReportPayload
{
    public ulong AssignmentEpoch { get; init; }

    public bool LocalReady { get; init; }

    public uint StatusFlags { get; init; }

    public IReadOnlyList<ServerStubReadyEntryPayload> Entries { get; init; } =
        Array.Empty<ServerStubReadyEntryPayload>();

    public ulong ReportedAtUnixMs { get; init; }
}

public sealed class ClusterReadyNotifyPayload
{
    public ulong ReadyEpoch { get; init; }

    public bool ClusterReady { get; init; }

    public uint StatusFlags { get; init; }

    public ulong ServerNowUnixMs { get; init; }
}

public sealed class RelayForwardMailboxCallPayload
{
    public string SourceGameNodeId { get; init; } = string.Empty;

    public string TargetGameNodeId { get; init; } = string.Empty;

    public string TargetEntityId { get; init; } = string.Empty;

    public string TargetMailboxName { get; init; } = string.Empty;

    public uint MailboxCallMsgId { get; init; }

    public uint RelayFlags { get; init; }

    public ReadOnlyMemory<byte> Payload { get; init; } = ReadOnlyMemory<byte>.Empty;
}

public sealed class RelayForwardProxyCallPayload
{
    public string SourceGameNodeId { get; init; } = string.Empty;

    public string RouteGateNodeId { get; init; } = string.Empty;

    public string TargetEntityId { get; init; } = string.Empty;

    public uint ProxyCallMsgId { get; init; }

    public uint RelayFlags { get; init; }

    public ReadOnlyMemory<byte> Payload { get; init; } = ReadOnlyMemory<byte>.Empty;
}

public sealed class RelayPushToClientPayload
{
    public string SourceGameNodeId { get; init; } = string.Empty;

    public string RouteGateNodeId { get; init; } = string.Empty;

    public string TargetEntityId { get; init; } = string.Empty;

    public uint ClientMsgId { get; init; }

    public uint RelayFlags { get; init; }

    public ReadOnlyMemory<byte> Payload { get; init; } = ReadOnlyMemory<byte>.Empty;
}

public sealed class GateCreateAvatarEntityPayload
{
    [JsonPropertyName("accountId")]
    public string? AccountId { get; init; }

    [JsonPropertyName("avatarId")]
    public string? AvatarId { get; init; }

    [JsonPropertyName("gateNodeId")]
    public string? GateNodeId { get; init; }

    [JsonPropertyName("sessionId")]
    public ulong SessionId { get; init; }
}

public sealed class GameAvatarEntityCreateResultPayload
{
    [JsonPropertyName("action")]
    public string Action { get; init; } = "createAvatarResult";

    [JsonPropertyName("success")]
    public bool Success { get; init; }

    [JsonPropertyName("sessionId")]
    public ulong SessionId { get; init; }

    [JsonPropertyName("accountId")]
    public string? AccountId { get; init; }

    [JsonPropertyName("avatarId")]
    public string? AvatarId { get; init; }

    [JsonPropertyName("gameNodeId")]
    public string? GameNodeId { get; init; }

    [JsonPropertyName("gateNodeId")]
    public string? GateNodeId { get; init; }

    [JsonPropertyName("error")]
    public string? Error { get; init; }
}

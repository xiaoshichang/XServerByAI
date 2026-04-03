namespace XServer.Client.Auth;

public sealed record GateLoginGrant(
    string GateNodeId,
    string Account,
    string KcpHost,
    int KcpPort,
    uint Conversation,
    DateTimeOffset IssuedAt,
    DateTimeOffset ExpiresAt);
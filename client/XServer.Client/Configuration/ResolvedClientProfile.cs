namespace XServer.Client.Configuration;

public sealed record ResolvedClientProfile(
    string ConfigPath,
    string GateNodeId,
    string Host,
    int Port,
    uint Conversation,
    KcpTransportOptions Kcp,
    string EndpointSource)
{
    public string DisplayEndpoint => $"{Host}:{Port}";
}

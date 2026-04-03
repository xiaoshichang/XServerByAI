namespace XServer.Client.Configuration;

public sealed record ResolvedClientProfile(
    string ConfigPath,
    string GateNodeId,
    string Host,
    int Port,
    uint Conversation,
    KcpTransportOptions Kcp,
    string AuthHost,
    int AuthPort,
    string EndpointSource)
{
    public string DisplayEndpoint => $"{Host}:{Port}";
    public string DisplayAuthEndpoint => $"{AuthHost}:{AuthPort}";

    public ResolvedClientProfile WithKcpSession(string host, int port, uint conversation, string endpointSource)
    {
        return this with
        {
            Host = host,
            Port = port,
            Conversation = conversation,
            EndpointSource = endpointSource,
        };
    }
}
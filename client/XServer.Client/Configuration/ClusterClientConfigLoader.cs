using System.Text.Json;

namespace XServer.Client.Configuration;

public static class ClusterClientConfigLoader
{
    public static ResolvedClientProfile Load(ClientLaunchOptions options)
    {
        string configPath = Path.GetFullPath(options.ConfigPath);
        if (!File.Exists(configPath))
        {
            throw new FileNotFoundException($"Cluster config file was not found: {configPath}", configPath);
        }

        using JsonDocument document = JsonDocument.Parse(File.ReadAllText(configPath));
        JsonElement root = document.RootElement;

        JsonElement kcpElement = GetRequiredProperty(root, "kcp", "root");
        JsonElement gateElement = GetRequiredProperty(root, "gate", "root");
        JsonElement gateInstanceElement = GetRequiredProperty(gateElement, options.GateNodeId, "gate");
        JsonElement clientNetworkElement = GetRequiredProperty(gateInstanceElement, "clientNetwork", $"gate.{options.GateNodeId}");
        JsonElement listenEndpointElement = GetRequiredProperty(
            clientNetworkElement,
            "listenEndpoint",
            $"gate.{options.GateNodeId}.clientNetwork");

        string configuredHost = GetRequiredString(listenEndpointElement, "host", "listenEndpoint");
        int configuredPort = GetRequiredPositiveInt32(listenEndpointElement, "port", "listenEndpoint");
        string host = options.HostOverride ?? NormalizeDialHost(configuredHost);
        int port = options.PortOverride ?? configuredPort;

        string endpointSource = options.HostOverride is not null || options.PortOverride is not null
            ? "command-line override"
            : configuredHost == host
                ? "cluster config"
                : $"cluster config ({configuredHost} normalized for dial)";

        return new ResolvedClientProfile(
            configPath,
            options.GateNodeId,
            host,
            port,
            options.Conversation,
            ParseKcpOptions(kcpElement),
            endpointSource);
    }

    private static KcpTransportOptions ParseKcpOptions(JsonElement kcpElement)
    {
        return new KcpTransportOptions(
            Mtu: GetRequiredPositiveInt32(kcpElement, "mtu", "kcp"),
            SendWindow: GetRequiredPositiveInt32(kcpElement, "sndwnd", "kcp"),
            ReceiveWindow: GetRequiredPositiveInt32(kcpElement, "rcvwnd", "kcp"),
            NoDelay: GetRequiredBoolean(kcpElement, "nodelay", "kcp"),
            IntervalMs: GetRequiredPositiveInt32(kcpElement, "intervalMs", "kcp"),
            FastResend: GetRequiredNonNegativeInt32(kcpElement, "fastResend", "kcp"),
            NoCongestionWindow: GetRequiredBoolean(kcpElement, "noCongestionWindow", "kcp"),
            MinRtoMs: GetRequiredPositiveInt32(kcpElement, "minRtoMs", "kcp"),
            DeadLinkCount: GetRequiredPositiveInt32(kcpElement, "deadLinkCount", "kcp"),
            StreamMode: GetRequiredBoolean(kcpElement, "streamMode", "kcp"));
    }

    private static JsonElement GetRequiredProperty(JsonElement element, string propertyName, string path)
    {
        if (!element.TryGetProperty(propertyName, out JsonElement property))
        {
            throw new InvalidDataException($"Missing required property '{path}.{propertyName}'.");
        }

        return property;
    }

    private static string GetRequiredString(JsonElement element, string propertyName, string path)
    {
        JsonElement property = GetRequiredProperty(element, propertyName, path);
        string? value = property.GetString();
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new InvalidDataException($"Property '{path}.{propertyName}' must not be empty.");
        }

        return value;
    }

    private static int GetRequiredPositiveInt32(JsonElement element, string propertyName, string path)
    {
        JsonElement property = GetRequiredProperty(element, propertyName, path);
        if (!property.TryGetInt32(out int value) || value <= 0)
        {
            throw new InvalidDataException($"Property '{path}.{propertyName}' must be a positive integer.");
        }

        return value;
    }

    private static int GetRequiredNonNegativeInt32(JsonElement element, string propertyName, string path)
    {
        JsonElement property = GetRequiredProperty(element, propertyName, path);
        if (!property.TryGetInt32(out int value) || value < 0)
        {
            throw new InvalidDataException($"Property '{path}.{propertyName}' must be a non-negative integer.");
        }

        return value;
    }

    private static bool GetRequiredBoolean(JsonElement element, string propertyName, string path)
    {
        JsonElement property = GetRequiredProperty(element, propertyName, path);
        if (property.ValueKind is not JsonValueKind.True and not JsonValueKind.False)
        {
            throw new InvalidDataException($"Property '{path}.{propertyName}' must be a boolean.");
        }

        return property.GetBoolean();
    }

    private static string NormalizeDialHost(string configuredHost)
    {
        return configuredHost switch
        {
            "0.0.0.0" => "127.0.0.1",
            "::" => "::1",
            "[::]" => "::1",
            _ => configuredHost,
        };
    }
}

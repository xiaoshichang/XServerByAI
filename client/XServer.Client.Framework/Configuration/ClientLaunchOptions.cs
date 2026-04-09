namespace XServer.Client.Configuration;

public sealed record ClientLaunchOptions(
    string ConfigPath,
    string GateNodeId,
    string? HostOverride,
    int? PortOverride,
    uint Conversation,
    string? ScriptPath,
    bool ExitAfterScript)
{
    public static ClientLaunchOptions Parse(IReadOnlyList<string> args)
    {
        string configPath = "configs/local-dev.json";
        string gateNodeId = "Gate0";
        string? hostOverride = null;
        int? portOverride = null;
        uint conversation = 1U;
        string? scriptPath = null;
        bool exitAfterScript = false;

        for (int index = 0; index < args.Count; index++)
        {
            string arg = args[index];
            switch (arg)
            {
            case "--config":
                configPath = ReadRequiredValue(args, ref index, arg);
                break;
            case "--gate":
                gateNodeId = ReadRequiredValue(args, ref index, arg);
                break;
            case "--host":
                hostOverride = ReadRequiredValue(args, ref index, arg);
                break;
            case "--port":
                portOverride = ParsePositiveInt32(ReadRequiredValue(args, ref index, arg), arg);
                break;
            case "--conversation":
                conversation = ParseUInt32(ReadRequiredValue(args, ref index, arg), arg);
                break;
            case "--script":
                scriptPath = ReadRequiredValue(args, ref index, arg);
                break;
            case "--exit-after-script":
                exitAfterScript = true;
                break;
            case "--help":
            case "-h":
                throw new ArgumentException(GetUsage());
            default:
                throw new ArgumentException($"Unknown argument '{arg}'.\n{GetUsage()}");
            }
        }

        return new ClientLaunchOptions(
            configPath,
            gateNodeId,
            hostOverride,
            portOverride,
            conversation,
            scriptPath,
            exitAfterScript);
    }

    public ClientLaunchOptions WithOverrides(
        string? configPath = null,
        string? gateNodeId = null,
        string? hostOverride = null,
        int? portOverride = null,
        uint? conversation = null,
        string? scriptPath = null,
        bool? exitAfterScript = null)
    {
        return this with
        {
            ConfigPath = configPath ?? ConfigPath,
            GateNodeId = gateNodeId ?? GateNodeId,
            HostOverride = hostOverride ?? HostOverride,
            PortOverride = portOverride ?? PortOverride,
            Conversation = conversation ?? Conversation,
            ScriptPath = scriptPath ?? ScriptPath,
            ExitAfterScript = exitAfterScript ?? ExitAfterScript,
        };
    }

    public static string GetUsage()
    {
        return string.Join(
            Environment.NewLine,
            "Usage:",
            "  dotnet run --project client/XServer.Client.App/XServer.Client.App.csproj [options]",
            "Options:",
            "  --config <path>         Cluster config path. Default: configs/local-dev.json",
            "  --gate <GateNodeId>     Gate instance id. Default: Gate0",
            "  --host <host>           Override remote client endpoint host.",
            "  --port <port>           Override remote client endpoint port.",
            "  --conversation <conv>   KCP conversation id. Default: 1",
            "  --script <path>         Execute a script before entering REPL.",
            "  --exit-after-script     Exit after the startup script finishes.",
            "  --help                  Show this help.");
    }

    private static string ReadRequiredValue(IReadOnlyList<string> args, ref int index, string optionName)
    {
        int valueIndex = index + 1;
        if (valueIndex >= args.Count)
        {
            throw new ArgumentException($"Missing value for {optionName}.");
        }

        index = valueIndex;
        return args[valueIndex];
    }

    private static int ParsePositiveInt32(string text, string optionName)
    {
        if (!int.TryParse(text, out int value) || value <= 0)
        {
            throw new ArgumentException($"Option {optionName} expects a positive integer but received '{text}'.");
        }

        return value;
    }

    private static uint ParseUInt32(string text, string optionName)
    {
        if (!uint.TryParse(text, out uint value))
        {
            throw new ArgumentException($"Option {optionName} expects an unsigned integer but received '{text}'.");
        }

        return value;
    }
}

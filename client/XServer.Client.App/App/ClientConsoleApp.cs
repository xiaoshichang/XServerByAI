using System.Text;
using XServer.Client.Auth;
using XServer.Client.Configuration;
using XServer.Client.GameLogic;
using XServer.Client.Rpc;
using XServer.Client.Runtime;
using XServer.Client.Transport;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.App;

public sealed class ClientConsoleApp
{
    private const uint DefaultClientHelloMsgId = 45010U;
    private const string DefaultGate1AuthUrl = "http://127.0.0.1:4101";
    private const string DefaultGate1NodeId = "Gate1";

    private readonly ClientLaunchOptions _launchOptions;
    private readonly TextWriter _output;
    private readonly TextWriter _error;
    private readonly ClientGameLogicService _gameLogic = new();
    private readonly ClientRuntimeState _state = new();

    private ClientTransport? _transport;

    public ClientConsoleApp(ClientLaunchOptions launchOptions, TextWriter output, TextWriter error)
    {
        _launchOptions = launchOptions;
        _output = output;
        _error = error;
    }

    public async Task<int> RunAsync()
    {
        await _output.WriteLineAsync("XServer.Client simulator");
        await _output.WriteLineAsync("Type 'help' for commands.");

        if (!string.IsNullOrWhiteSpace(_launchOptions.ScriptPath))
        {
            bool continueApplication = await RunScriptAsync(_launchOptions.ScriptPath!, CancellationToken.None);
            if (!continueApplication || _launchOptions.ExitAfterScript)
            {
                await DisposeTransportAsync();
                return 0;
            }
        }

        while (true)
        {
            await _output.WriteAsync("> ");
            string? line = await Console.In.ReadLineAsync();
            if (line is null)
            {
                break;
            }

            if (!ParsedCommand.TryParse(line, out ParsedCommand? command, out string? error))
            {
                await WriteErrorAsync(error ?? "Failed to parse the command line.");
                continue;
            }

            if (command is null)
            {
                continue;
            }

            try
            {
                bool continueApplication = await ExecuteCommandAsync(command, CancellationToken.None);
                if (!continueApplication)
                {
                    break;
                }
            }
            catch (Exception exception)
            {
                await WriteErrorAsync(exception.Message);
            }
        }

        await DisposeTransportAsync();
        return 0;
    }

    private async Task<bool> ExecuteCommandAsync(ParsedCommand command, CancellationToken cancellationToken)
    {
        switch (command.Name.ToLowerInvariant())
        {
        case "help":
            await PrintHelpAsync();
            return true;

        case "connect":
            await HandleConnectAsync(command, cancellationToken);
            return true;

        case "disconnect":
            await DisposeTransportAsync();
            _state.MarkDisconnected();
            await _output.WriteLineAsync("disconnected");
            return true;

        case "status":
            await PrintStatusAsync();
            return true;

        case "send":
            await HandleSendAsync(command, cancellationToken);
            return true;

        case "login":
            await HandleLoginAsync(command, cancellationToken);
            return true;

        case "selectavatar":
        case "select-avatar":
            await HandleSelectAvatarAsync(command, cancellationToken);
            return true;

        case "move":
            await HandleMoveAsync(command, cancellationToken);
            return true;

        case "buyweapon":
        case "buy-weapon":
            await HandleBuyWeaponAsync(command, cancellationToken);
            return true;

        case "setweapon":
        case "set-weapon":
            await HandleSetWeaponAsync(command, cancellationToken);
            return true;

        case "script":
            return await HandleScriptCommandAsync(command, cancellationToken);

        case "exit":
        case "quit":
            return false;

        default:
            throw new ArgumentException($"Unknown command '{command.Name}'. Type 'help' to see available commands.");
        }
    }

    private async Task HandleConnectAsync(ParsedCommand command, CancellationToken cancellationToken)
    {
        ClientLaunchOptions effectiveOptions = _launchOptions.WithOverrides(
            configPath: command.GetOptionalString("config"),
            gateNodeId: command.GetOptionalString("gate"),
            hostOverride: command.GetOptionalString("host"),
            portOverride: command.HasOption("port") ? command.GetInt32OrDefault("port", 0) : null,
            conversation: command.HasOption("conversation") ? command.GetUInt32OrDefault("conversation", 0U) : null);

        ResolvedClientProfile configuredProfile = ClusterClientConfigLoader.Load(effectiveOptions);
        ResolvedClientProfile profile = configuredProfile;
        if (!command.HasOption("gate") &&
            !command.HasOption("host") &&
            !command.HasOption("port") &&
            !command.HasOption("conversation") &&
            _state.TryGetCachedLoginProfile(configuredProfile.ConfigPath, out ResolvedClientProfile cachedLoginProfile))
        {
            profile = cachedLoginProfile;
        }

        await DisposeTransportAsync();

        ClientTransport transport = new(profile);
        transport.Trace += HandleTransportTrace;
        transport.PacketReceived += HandlePacketReceived;
        transport.RawPayloadReceived += HandleRawPayloadReceived;
        await transport.ConnectAsync(cancellationToken);

        _transport = transport;
        _state.MarkConnected(profile, transport.LocalEndpointText);
        try
        {
            await SendClientHelloAsync(transport, cancellationToken);
        }
        catch
        {
            await DisposeTransportAsync();
            _state.MarkDisconnected();
            throw;
        }
        _state.ConfigureRpcSender(CreateRpcSender(transport));
        await _output.WriteLineAsync(
            $"connected remote={profile.DisplayEndpoint} gate={profile.GateNodeId} conv={profile.Conversation} source={profile.EndpointSource}; session primed with clientHello");
    }

    private async Task HandleSendAsync(ParsedCommand command, CancellationToken cancellationToken)
    {
        ClientTransport transport = RequireTransport();
        uint msgId = command.GetRequiredUInt32("msgId");
        if (msgId == 0U)
        {
            throw new ArgumentException("msgId must not be zero.");
        }

        PacketFlags flags = ParsePacketFlags(command.GetOptionalString("flags"));
        byte[] payload = BuildPayloadBytes(command);
        uint sequence = command.GetUInt32OrDefault("seq", _state.AllocatePacketSequence());
        PacketHeader header = PacketCodec.CreateHeader(msgId, sequence, flags, checked((uint)payload.Length));

        await transport.SendPacketAsync(header, payload, cancellationToken);
        _state.RecordSentPacket(header);
        await _output.WriteLineAsync(
            $"sent msgId={header.MsgId} seq={header.Seq} flags={header.Flags} payloadBytes={payload.Length}");
    }

    private async Task HandleLoginAsync(ParsedCommand command, CancellationToken cancellationToken)
    {
        ClientLaunchOptions effectiveOptions = _launchOptions.WithOverrides(
            configPath: command.GetOptionalString("config"),
            gateNodeId: DefaultGate1NodeId);

        ResolvedClientProfile baseProfile = ClusterClientConfigLoader.Load(effectiveOptions);
        (string loginUrl, string accountId, string password) = ResolveLoginRequest(command);

        using HttpClient httpClient = new();
        GateAuthClient authClient = new(httpClient);
        GateLoginGrant grant = await authClient.LoginAsync(
            loginUrl,
            DefaultGate1NodeId,
            accountId,
            password,
            cancellationToken);

        ResolvedClientProfile grantedProfile = baseProfile.WithKcpSession(
            grant.KcpHost,
            grant.KcpPort,
            grant.Conversation,
            "http login");
        _state.StoreLoginGrant(grant.AccountId, grantedProfile, grant.IssuedAt, grant.ExpiresAt);
        await _output.WriteLineAsync(
            $"http login succeeded account={grant.AccountId} kcp={grantedProfile.DisplayEndpoint} conv={grant.Conversation} expiresAt={grant.ExpiresAt:O}. local Account cached; run connect to open and prime the KCP session, then selectAvatar to enter game.");
    }

    private async Task HandleSelectAvatarAsync(ParsedCommand command, CancellationToken cancellationToken)
    {
        RequireTransport();

        if (command.HasOption("avatarId") || command.HasOption("avatarName"))
        {
            throw new ArgumentException(
                "selectAvatar no longer accepts avatarId/avatarName parameters. It now always uses temporary random placeholder data.");
        }

        uint? msgId = command.HasOption("msgId")
            ? command.GetUInt32OrDefault("msgId", ClientGameLogicService.DefaultSelectAvatarMsgId)
            : null;
        OutboundGameRequest request = _gameLogic.PrepareSelectAvatarRequest(_state, msgId);
        await SendGameRequestAsync(request, cancellationToken);
    }

    private async Task SendClientHelloAsync(ClientTransport transport, CancellationToken cancellationToken)
    {
        PacketHeader header = PacketCodec.CreateHeader(
            DefaultClientHelloMsgId,
            _state.AllocatePacketSequence(),
            PacketFlags.None,
            0U);
        await transport.SendPacketAsync(header, ReadOnlyMemory<byte>.Empty, cancellationToken);
        _state.RecordSentPacket(header);
        await _output.WriteLineAsync(
            $"clientHello sent msgId={header.MsgId} seq={header.Seq} payloadBytes=0");
    }

    private async Task HandleMoveAsync(ParsedCommand command, CancellationToken cancellationToken)
    {
        RequireTransport();

        float? x = command.HasOption("x") ? command.GetSingleOrDefault("x", 0.0f) : null;
        float? y = command.HasOption("y") ? command.GetSingleOrDefault("y", 0.0f) : null;
        float? z = command.HasOption("z") ? command.GetSingleOrDefault("z", 0.0f) : null;
        bool localApply = command.GetBooleanOrDefault("localApply", true);
        uint? msgId = command.HasOption("msgId")
            ? command.GetUInt32OrDefault("msgId", ClientGameLogicService.DefaultMoveMsgId)
            : null;
        OutboundGameRequest request = _gameLogic.PrepareMoveRequest(_state, x, y, z, localApply, msgId);
        await SendGameRequestAsync(request, cancellationToken);
    }

    private async Task HandleBuyWeaponAsync(ParsedCommand command, CancellationToken cancellationToken)
    {
        RequireTransport();

        string weaponId = command.GetStringOrDefault("weaponId", "training-sword");
        int count = command.GetInt32OrDefault("count", 1);
        bool localApply = command.GetBooleanOrDefault("localApply", true);
        uint? msgId = command.HasOption("msgId")
            ? command.GetUInt32OrDefault("msgId", ClientGameLogicService.DefaultBuyWeaponMsgId)
            : null;
        OutboundGameRequest request = _gameLogic.PrepareBuyWeaponRequest(_state, weaponId, count, localApply, msgId);
        await SendGameRequestAsync(request, cancellationToken);
    }

    private async Task HandleSetWeaponAsync(ParsedCommand command, CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        RequireTransport();

        string weapon = ResolveSetWeapon(command);
        string summary = _gameLogic.SendSetWeaponRpc(_state, weapon);
        await _output.WriteLineAsync(summary);
    }

    private async Task<bool> HandleScriptCommandAsync(ParsedCommand command, CancellationToken cancellationToken)
    {
        string path = command.GetRequiredString("path");
        return await RunScriptAsync(path, cancellationToken, command.GetBooleanOrDefault("continueOnError", false));
    }

    private async Task<bool> RunScriptAsync(
        string scriptPath,
        CancellationToken cancellationToken,
        bool continueOnError = false)
    {
        return await ClientScriptRunner.RunAsync(
            scriptPath,
            ExecuteCommandAsync,
            message => _output.WriteLine(message),
            message => _error.WriteLine(message),
            continueOnError,
            cancellationToken);
    }

    private async Task PrintHelpAsync()
    {
        string helpText = string.Join(
            Environment.NewLine,
            "Commands:",
            "  connect [config=path] [gate=Gate0] [host=127.0.0.1] [port=4000] [conversation=1]",
            "  disconnect",
            "  status",
            "  send msgId=45050 [text=\"hello\"] [json=\"{\\\"k\\\":1}\"] [flags=response,error,compressed] [seq=1]",
            "  login <url> <account> <password> [config=path]",
            "  connect auto-sends clientHello [msgId=45010] to prime the Gate session",
            $"  selectAvatar [msgId={ClientGameLogicService.DefaultSelectAvatarMsgId}]",
            $"  move [x=1] [y=2] [z=0] [msgId={ClientGameLogicService.DefaultMoveMsgId}] [localApply=true]",
            $"  buyWeapon [weaponId=rifle] [count=1] [msgId={ClientGameLogicService.DefaultBuyWeaponMsgId}] [localApply=true]",
            "  set-weapon <weapon>",
            "  script path=client/demo.txt [continueOnError=true]",
            "  quit | exit",
            "",
            "Notes:",
            "  login caches the local Account after the HTTP grant succeeds, but does not auto-select an Avatar.",
            "  connect now sends a lightweight clientHello packet immediately after the UDP/KCP transport opens.",
            "  selectAvatar sends a placeholder choose-avatar request to Gate and locally enters a waiting-for-confirmation state.",
            "  selectAvatar now always generates temporary random avatarId/avatarName placeholders locally.",
            "  Avatar-dependent commands become available only after Gate confirms AvatarEntity creation.",
            "  set-weapon sends a client entity RPC [msgId=6302] to the selected Avatar on Game.",
            $"  demo default login url is {DefaultGate1AuthUrl} (Gate1 auth).",
            "  move/buyWeapon use temporary test-range msgIds by default and can be overridden.",
            "  connect reads configs/local-dev.json and Gate0 by default.",
            "  After login, connect without overrides reuses the most recent granted KCP endpoint.",
            "  If the Gate config binds clientNetwork.listenEndpoint.host to 0.0.0.0, the simulator dials 127.0.0.1 by default.");
        await _output.WriteLineAsync(helpText);
    }

    private async Task PrintStatusAsync()
    {
        int pendingAckCount = _transport?.PendingAcknowledgementCount ?? 0;
        uint nextSendSequence = _transport?.NextKcpSendSequence ?? 0U;
        uint nextReceiveSequence = _transport?.NextKcpReceiveSequence ?? 0U;
        await _output.WriteLineAsync(_state.BuildStatusText(pendingAckCount, nextSendSequence, nextReceiveSequence));
    }

    private async Task DisposeTransportAsync()
    {
        _state.ConfigureRpcSender(null);
        if (_transport is null)
        {
            return;
        }

        _transport.Trace -= HandleTransportTrace;
        _transport.PacketReceived -= HandlePacketReceived;
        _transport.RawPayloadReceived -= HandleRawPayloadReceived;
        await _transport.DisposeAsync();
        _transport = null;
    }

    private void HandleTransportTrace(string message)
    {
        _output.WriteLine($"trace: {message}");
    }

    private void HandlePacketReceived(PacketView packet)
    {
        _state.RecordReceivedPacket(packet.Header);
        string payloadPreview = TryFormatUtf8(packet.Payload);
        _output.WriteLine(
            $"recv msgId={packet.Header.MsgId} seq={packet.Header.Seq} flags={packet.Header.Flags} " +
            $"payloadBytes={packet.Payload.Length} payload={payloadPreview}");

        string? controlMessage = _gameLogic.TryHandleControlPacket(_state, packet);
        if (!string.IsNullOrWhiteSpace(controlMessage))
        {
            _output.WriteLine(controlMessage);
        }
    }

    private void HandleRawPayloadReceived(ReadOnlyMemory<byte> payload, string error)
    {
        _output.WriteLine(
            $"recv raw payloadBytes={payload.Length} decodeError=\"{error}\" preview={HexPreview(payload.Span, 48)}");
    }

    private IClientEntityRpcSender CreateRpcSender(ClientTransport transport)
    {
        return new ClientEntityRpcPacketSender(
            _state,
            (header, payload) => transport.SendPacketAsync(header, payload, CancellationToken.None).GetAwaiter().GetResult());
    }

    private ClientTransport RequireTransport()
    {
        return _transport ?? throw new InvalidOperationException("The simulated client is not connected. Run 'connect' first.");
    }

    private static string ResolveSetWeapon(ParsedCommand command)
    {
        if (command.Positionals.Count > 1)
        {
            throw new ArgumentException("set-weapon expects exactly one weapon name, for example: set-weapon gun");
        }

        if (command.Positionals.Count == 1)
        {
            if (command.HasOption("weapon"))
            {
                throw new ArgumentException("set-weapon accepts either a positional weapon name or weapon=..., not both.");
            }

            return command.Positionals[0];
        }

        string? optionWeapon = command.GetOptionalString("weapon");
        if (!string.IsNullOrWhiteSpace(optionWeapon))
        {
            return optionWeapon;
        }

        throw new ArgumentException("set-weapon expects a weapon name, for example: set-weapon gun");
    }

    private static (string Url, string Account, string Password) ResolveLoginRequest(ParsedCommand command)
    {
        if (command.Positionals.Count == 3)
        {
            string url = command.Positionals[0];
            string account = command.Positionals[1];
            string password = command.Positionals[2];
            if (!string.IsNullOrWhiteSpace(url) &&
                !string.IsNullOrWhiteSpace(account) &&
                !string.IsNullOrWhiteSpace(password))
            {
                return (url, account, password);
            }
        }

        string? optionAccount = command.GetOptionalString("account");
        string? optionPassword = command.GetOptionalString("password");
        if (!string.IsNullOrWhiteSpace(optionAccount) && !string.IsNullOrWhiteSpace(optionPassword))
        {
            return (DefaultGate1AuthUrl, optionAccount, optionPassword);
        }

        throw new ArgumentException(
            $"login expects url, account and password, for example: login {DefaultGate1AuthUrl} demo-account dev-password");
    }

    private static PacketFlags ParsePacketFlags(string? text)
    {
        if (string.IsNullOrWhiteSpace(text) || string.Equals(text, "none", StringComparison.OrdinalIgnoreCase))
        {
            return PacketFlags.None;
        }

        PacketFlags flags = PacketFlags.None;
        foreach (string segment in text.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            flags |= segment.ToLowerInvariant() switch
            {
                "response" => PacketFlags.Response,
                "compressed" => PacketFlags.Compressed,
                "error" => PacketFlags.Error,
                _ => throw new ArgumentException($"Unknown packet flag '{segment}'."),
            };
        }

        return flags;
    }

    private async Task SendGameRequestAsync(OutboundGameRequest request, CancellationToken cancellationToken)
    {
        ClientTransport transport = RequireTransport();
        await transport.SendPacketAsync(request.Header, request.Payload, cancellationToken);
        _state.RecordSentPacket(request.Header);
        request.ApplyAfterSend?.Invoke();
        await _output.WriteLineAsync(request.Summary);
    }

    private static byte[] BuildPayloadBytes(ParsedCommand command)
    {
        string? json = command.GetOptionalString("json");
        string? text = command.GetOptionalString("text");

        if (json is not null && text is not null)
        {
            throw new ArgumentException("Choose either text=... or json=..., not both.");
        }

        if (json is not null)
        {
            return Encoding.UTF8.GetBytes(json);
        }

        if (text is not null)
        {
            return Encoding.UTF8.GetBytes(text);
        }

        return Array.Empty<byte>();
    }

    private static string TryFormatUtf8(ReadOnlyMemory<byte> payload)
    {
        if (payload.IsEmpty)
        {
            return "<empty>";
        }

        try
        {
            string text = Encoding.UTF8.GetString(payload.Span);
            if (text.All(character => !char.IsControl(character) || character is '\r' or '\n' or '\t'))
            {
                return text;
            }
        }
        catch (DecoderFallbackException)
        {
        }

        return HexPreview(payload.Span, 48);
    }

    private static string HexPreview(ReadOnlySpan<byte> bytes, int maxBytes)
    {
        int previewLength = Math.Min(bytes.Length, maxBytes);
        StringBuilder builder = new(previewLength * 3);
        for (int index = 0; index < previewLength; index++)
        {
            if (index > 0)
            {
                builder.Append(' ');
            }

            builder.Append(bytes[index].ToString("X2"));
        }

        if (bytes.Length > previewLength)
        {
            builder.Append(" ...");
        }

        return builder.ToString();
    }

    private Task WriteErrorAsync(string message)
    {
        return _error.WriteLineAsync($"error: {message}");
    }
}

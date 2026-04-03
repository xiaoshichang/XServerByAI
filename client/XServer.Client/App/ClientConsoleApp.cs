using System.Text;
using System.Text.Json;
using XServer.Client.Auth;
using XServer.Client.Configuration;
using XServer.Client.Runtime;
using XServer.Client.Transport;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.App;

public sealed class ClientConsoleApp
{
    private const uint DefaultLoginMsgId = 45001U;
    private const uint DefaultMoveMsgId = 45011U;
    private const uint DefaultBuyWeaponMsgId = 45012U;

    private readonly ClientLaunchOptions _launchOptions;
    private readonly TextWriter _output;
    private readonly TextWriter _error;
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

        case "move":
            await HandleMoveAsync(command, cancellationToken);
            return true;

        case "buyweapon":
        case "buy-weapon":
            await HandleBuyWeaponAsync(command, cancellationToken);
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
        if (!command.HasOption("host") &&
            !command.HasOption("port") &&
            !command.HasOption("conversation") &&
            _state.TryGetCachedLoginProfile(configuredProfile.ConfigPath, configuredProfile.GateNodeId, out ResolvedClientProfile cachedLoginProfile))
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
        await _output.WriteLineAsync(
            $"connected remote={profile.DisplayEndpoint} gate={profile.GateNodeId} conv={profile.Conversation} source={profile.EndpointSource}");
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
            gateNodeId: command.GetOptionalString("gate"));

        ResolvedClientProfile baseProfile = ClusterClientConfigLoader.Load(effectiveOptions);
        string account = command.GetStringOrDefault("account", "dev-account");
        string password = command.GetStringOrDefault("password", "dev-password");
        bool localSuccess = command.GetBooleanOrDefault("localSuccess", false);
        long playerId = command.GetInt32OrDefault("playerId", 10001);
        string? avatarId = command.GetOptionalString("avatarId");

        using HttpClient httpClient = new();
        GateAuthClient authClient = new(httpClient);
        GateLoginGrant grant = await authClient.LoginAsync(
            baseProfile.AuthHost,
            baseProfile.AuthPort,
            effectiveOptions.GateNodeId,
            account,
            password,
            cancellationToken);

        ResolvedClientProfile grantedProfile = baseProfile.WithKcpSession(
            grant.KcpHost,
            grant.KcpPort,
            grant.Conversation,
            "http login");
        _state.StoreLoginGrant(grant.Account, grantedProfile, grant.IssuedAt, grant.ExpiresAt);

        if (localSuccess)
        {
            _state.MarkLocalAvatarReady(playerId, avatarId);
            await _output.WriteLineAsync(
                $"http login succeeded account={grant.Account} kcp={grantedProfile.DisplayEndpoint} conv={grant.Conversation} expiresAt={grant.ExpiresAt:O}. local Avatar prepared.");
        }
        else
        {
            await _output.WriteLineAsync(
                $"http login succeeded account={grant.Account} kcp={grantedProfile.DisplayEndpoint} conv={grant.Conversation} expiresAt={grant.ExpiresAt:O}. run connect to open the KCP session.");
        }
    }

    private async Task HandleMoveAsync(ParsedCommand command, CancellationToken cancellationToken)
    {
        ClientTransport transport = RequireTransport();
        EnsureAvatarReady();

        float x = command.GetSingleOrDefault("x", _state.Avatar!.PositionX);
        float y = command.GetSingleOrDefault("y", _state.Avatar!.PositionY);
        float z = command.GetSingleOrDefault("z", _state.Avatar!.PositionZ);
        bool localApply = command.GetBooleanOrDefault("localApply", true);
        uint msgId = command.GetUInt32OrDefault("msgId", DefaultMoveMsgId);

        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(
            new
            {
                action = "move",
                avatarId = _state.Avatar!.AvatarId,
                position = new { x, y, z },
            });

        PacketHeader header = PacketCodec.CreateHeader(
            msgId,
            _state.AllocatePacketSequence(),
            PacketFlags.None,
            checked((uint)payload.Length));

        await transport.SendPacketAsync(header, payload, cancellationToken);
        _state.RecordSentPacket(header);

        if (localApply)
        {
            _state.UpdateAvatarPosition(x, y, z);
        }

        await _output.WriteLineAsync(
            $"move request sent msgId={msgId} pos=({x}, {y}, {z}) localApply={localApply}");
    }

    private async Task HandleBuyWeaponAsync(ParsedCommand command, CancellationToken cancellationToken)
    {
        ClientTransport transport = RequireTransport();
        EnsureAvatarReady();

        string weaponId = command.GetStringOrDefault("weaponId", "training-sword");
        int count = command.GetInt32OrDefault("count", 1);
        if (count <= 0)
        {
            throw new ArgumentException("count must be greater than zero.");
        }

        bool localApply = command.GetBooleanOrDefault("localApply", true);
        uint msgId = command.GetUInt32OrDefault("msgId", DefaultBuyWeaponMsgId);

        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(
            new
            {
                action = "buyWeapon",
                avatarId = _state.Avatar!.AvatarId,
                weaponId,
                count,
            });

        PacketHeader header = PacketCodec.CreateHeader(
            msgId,
            _state.AllocatePacketSequence(),
            PacketFlags.None,
            checked((uint)payload.Length));

        await transport.SendPacketAsync(header, payload, cancellationToken);
        _state.RecordSentPacket(header);

        if (localApply)
        {
            _state.AddWeapon(weaponId, count);
        }

        await _output.WriteLineAsync(
            $"buyWeapon request sent msgId={msgId} weaponId={weaponId} count={count} localApply={localApply}");
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
            "  login [account=dev-account] [password=dev-password] [config=path] [gate=Gate0] [localSuccess=true] [playerId=10001] [avatarId=avatar:dev-account]",
            "  move [x=1] [y=2] [z=0] [msgId=45011] [localApply=true]",
            "  buyWeapon [weaponId=rifle] [count=1] [msgId=45012] [localApply=true]",
            "  script path=client/demo.txt [continueOnError=true]",
            "  quit | exit",
            "",
            "Notes:",
            "  login/move/buyWeapon use temporary test-range msgIds by default and can be overridden.",
            "  connect reads configs/local-dev.json and Gate0 by default.",
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
    }

    private void HandleRawPayloadReceived(ReadOnlyMemory<byte> payload, string error)
    {
        _output.WriteLine(
            $"recv raw payloadBytes={payload.Length} decodeError=\"{error}\" preview={HexPreview(payload.Span, 48)}");
    }

    private ClientTransport RequireTransport()
    {
        return _transport ?? throw new InvalidOperationException("The simulated client is not connected. Run 'connect' first.");
    }

    private void EnsureAvatarReady()
    {
        if (!_state.HasAvatar)
        {
            throw new InvalidOperationException(
                "No local Avatar is ready. Run login with localSuccess=true before move/buyWeapon in M4-03.");
        }
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

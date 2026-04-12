using System.Net;
using System.Net.Sockets;
using XServer.Client.Configuration;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.Transport;

public sealed class ClientTransport : IAsyncDisposable
{
    private readonly ResolvedClientProfile _profile;
    private readonly MinimalKcpSession _kcpSession;
    private UdpClient? _udpClient;
    private CancellationTokenSource? _receiveLoopCancellationSource;
    private Task? _receiveLoopTask;

    public ClientTransport(ResolvedClientProfile profile)
    {
        _profile = profile;
        _kcpSession = new MinimalKcpSession(profile.Conversation, profile.Kcp);
    }

    public event Action<string>? Trace;
    public event Action<PacketView>? PacketReceived;

    public bool IsConnected => _udpClient is not null;
    public string? LocalEndpointText { get; private set; }
    public int PendingAcknowledgementCount => _kcpSession.PendingAcknowledgementCount;
    public uint NextKcpSendSequence => _kcpSession.NextSendSequence;
    public uint NextKcpReceiveSequence => _kcpSession.NextReceiveSequence;

    public async Task ConnectAsync(CancellationToken cancellationToken = default)
    {
        if (IsConnected)
        {
            return;
        }

        UdpClient udpClient = new(AddressFamily.InterNetwork);
        udpClient.Client.Bind(new IPEndPoint(IPAddress.Any, 0));
        await udpClient.Client.ConnectAsync(_profile.Host, _profile.Port, cancellationToken);

        _udpClient = udpClient;
        _receiveLoopCancellationSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        _receiveLoopTask = Task.Run(() => ReceiveLoopAsync(_receiveLoopCancellationSource.Token), CancellationToken.None);

        if (udpClient.Client.LocalEndPoint is IPEndPoint localEndpoint)
        {
            LocalEndpointText = $"{localEndpoint.Address}:{localEndpoint.Port}";
        }

        Trace?.Invoke($"transport connected to {_profile.DisplayEndpoint} conv={_profile.Conversation}");
    }

    public async Task DisconnectAsync()
    {
        if (!IsConnected)
        {
            return;
        }

        CancellationTokenSource? cancellationSource = _receiveLoopCancellationSource;
        Task? receiveLoopTask = _receiveLoopTask;
        UdpClient? udpClient = _udpClient;

        _receiveLoopCancellationSource = null;
        _receiveLoopTask = null;
        _udpClient = null;
        LocalEndpointText = null;

        try
        {
            cancellationSource?.Cancel();
            udpClient?.Dispose();

            if (receiveLoopTask is not null)
            {
                await receiveLoopTask;
            }
        }
        catch (OperationCanceledException)
        {
        }
        finally
        {
            cancellationSource?.Dispose();
        }

        Trace?.Invoke("transport disconnected");
    }

    public async Task SendPacketAsync(
        PacketHeader header,
        ReadOnlyMemory<byte> payload,
        CancellationToken cancellationToken = default)
    {
        UdpClient udpClient = _udpClient ?? throw new InvalidOperationException("Client transport is not connected.");

        PacketCodecErrorCode wireSizeResult = PacketCodec.GetWireSize(payload.Length, out int wireSize);
        if (wireSizeResult != PacketCodecErrorCode.None)
        {
            throw new InvalidOperationException(PacketCodec.GetErrorMessage(wireSizeResult));
        }

        byte[] packet = GC.AllocateUninitializedArray<byte>(wireSize);
        PacketCodecErrorCode encodeResult = PacketCodec.EncodePacket(header, payload.Span, packet);
        if (encodeResult != PacketCodecErrorCode.None)
        {
            throw new InvalidOperationException(PacketCodec.GetErrorMessage(encodeResult));
        }

        if (!_kcpSession.TryQueuePayload(packet, CurrentKcpClockMilliseconds(), out IReadOnlyList<byte[]> datagrams, out string? error))
        {
            throw new InvalidOperationException(error);
        }

        foreach (byte[] datagram in datagrams)
        {
            await udpClient.SendAsync(datagram, cancellationToken);
        }

        Trace?.Invoke(
            $"sent msgId={header.MsgId} seq={header.Seq} flags={header.Flags} payloadBytes={payload.Length} datagrams={datagrams.Count}");
    }

    public async ValueTask DisposeAsync()
    {
        await DisconnectAsync();
    }

    private async Task ReceiveLoopAsync(CancellationToken cancellationToken)
    {
        if (_udpClient is null)
        {
            return;
        }

        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                UdpReceiveResult receiveResult = await _udpClient.ReceiveAsync(cancellationToken);
                await HandleInboundDatagramAsync(receiveResult.Buffer, cancellationToken);
            }
        }
        catch (ObjectDisposedException)
        {
        }
        catch (OperationCanceledException)
        {
        }
    }

    private async Task HandleInboundDatagramAsync(byte[] datagram, CancellationToken cancellationToken)
    {
        if (_udpClient is null)
        {
            return;
        }

        if (!_kcpSession.TryProcessInboundDatagram(
                datagram,
                CurrentKcpClockMilliseconds(),
                out MinimalKcpInboundResult inboundResult,
                out string? error))
        {
            Trace?.Invoke($"ignored inbound datagram: {error}");
            return;
        }

        foreach (string traceMessage in inboundResult.TraceMessages)
        {
            Trace?.Invoke(traceMessage);
        }

        foreach (byte[] controlDatagram in inboundResult.OutgoingDatagrams)
        {
            await _udpClient.SendAsync(controlDatagram, cancellationToken);
        }

        foreach (ReadOnlyMemory<byte> payload in inboundResult.ReceivedPayloads)
        {
            PacketCodecErrorCode decodeResult = PacketCodec.DecodePacket(payload, out PacketView packet);
            if (decodeResult == PacketCodecErrorCode.None)
            {
                PacketReceived?.Invoke(packet);
            }
            else
            {
                Trace?.Invoke(
                    $"recv raw payloadBytes={payload.Length} decodeError=\"{PacketCodec.GetErrorMessage(decodeResult)}\" preview={HexPreview(payload.Span, 48)}");
            }
        }
    }

    private static uint CurrentKcpClockMilliseconds()
    {
        return unchecked((uint)Environment.TickCount64);
    }

    private static string HexPreview(ReadOnlySpan<byte> bytes, int maxBytes)
    {
        int previewLength = Math.Min(bytes.Length, maxBytes);
        return bytes.Length <= maxBytes
            ? Convert.ToHexString(bytes[..previewLength])
            : $"{Convert.ToHexString(bytes[..previewLength])}...";
    }
}

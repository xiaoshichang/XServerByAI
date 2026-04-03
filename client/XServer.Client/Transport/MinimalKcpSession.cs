using XServer.Client.Configuration;
using XServer.Client.Protocol;

namespace XServer.Client.Transport;

public sealed class MinimalKcpSession
{
    private readonly KcpTransportOptions _options;
    private readonly Dictionary<uint, DateTimeOffset> _pendingOutbound = new();
    private readonly SortedDictionary<uint, KcpSegment> _receivedSegments = new();

    public MinimalKcpSession(uint conversation, KcpTransportOptions options)
    {
        if (conversation == 0U)
        {
            throw new ArgumentOutOfRangeException(nameof(conversation), "KCP conversation must be non-zero.");
        }

        if (options.MaxDatagramPayloadBytes <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(options), "KCP mtu must leave room for the 24-byte KCP header.");
        }

        Conversation = conversation;
        _options = options;
        RemoteWindow = ClampWindow(options.SendWindow);
    }

    public uint Conversation { get; }
    public uint NextSendSequence { get; private set; }
    public uint NextReceiveSequence { get; private set; }
    public ushort RemoteWindow { get; private set; }
    public int PendingAcknowledgementCount => _pendingOutbound.Count;

    public bool TryQueuePayload(
        ReadOnlyMemory<byte> payload,
        uint nowMilliseconds,
        out IReadOnlyList<byte[]> datagrams,
        out string? error)
    {
        if (payload.IsEmpty)
        {
            datagrams = Array.Empty<byte[]>();
            error = "KCP payload must not be empty.";
            return false;
        }

        if (payload.Length > _options.MaxDatagramPayloadBytes)
        {
            datagrams = Array.Empty<byte[]>();
            error =
                $"KCP payload length {payload.Length} exceeds the current simulator limit {_options.MaxDatagramPayloadBytes}. " +
                "Fragmented client payloads are not implemented in M4-03.";
            return false;
        }

        uint sequenceNumber = NextSendSequence++;
        _pendingOutbound[sequenceNumber] = DateTimeOffset.UtcNow;

        KcpSegment segment = new(
            Conversation,
            KcpCommand.Push,
            0,
            ClampWindow(_options.ReceiveWindow),
            nowMilliseconds,
            sequenceNumber,
            NextReceiveSequence,
            payload);

        datagrams = [KcpDatagramCodec.EncodeSegment(segment)];
        error = null;
        return true;
    }

    public bool TryProcessInboundDatagram(
        ReadOnlyMemory<byte> datagram,
        uint nowMilliseconds,
        out MinimalKcpInboundResult result,
        out string? error)
    {
        if (!KcpDatagramCodec.TryDecodeDatagram(datagram, out IReadOnlyList<KcpSegment> segments, out error))
        {
            result = new MinimalKcpInboundResult(Array.Empty<byte[]>(), Array.Empty<ReadOnlyMemory<byte>>(), Array.Empty<string>());
            return false;
        }

        List<byte[]> outgoingDatagrams = [];
        List<ReadOnlyMemory<byte>> receivedPayloads = [];
        List<string> traceMessages = [];

        foreach (KcpSegment segment in segments)
        {
            if (segment.Conversation != Conversation)
            {
                result = new MinimalKcpInboundResult(Array.Empty<byte[]>(), Array.Empty<ReadOnlyMemory<byte>>(), Array.Empty<string>());
                error = $"Received datagram for conversation {segment.Conversation}, expected {Conversation}.";
                return false;
            }

            RemoteWindow = segment.Window;
            switch (segment.Command)
            {
            case KcpCommand.Ack:
                _pendingOutbound.Remove(segment.SequenceNumber);
                traceMessages.Add($"ack sn={segment.SequenceNumber}");
                break;

            case KcpCommand.Push:
                outgoingDatagrams.Add(CreateAckDatagram(segment));
                if (segment.Fragment != 0)
                {
                    traceMessages.Add(
                        $"ignored fragmented push sn={segment.SequenceNumber} frg={segment.Fragment}; " +
                        "fragmented inbound KCP payloads are not implemented in M4-03.");
                    break;
                }

                if (segment.SequenceNumber < NextReceiveSequence)
                {
                    traceMessages.Add($"ignored duplicate push sn={segment.SequenceNumber}");
                    break;
                }

                _receivedSegments[segment.SequenceNumber] = segment;
                DrainReceivedPayloads(receivedPayloads, traceMessages);
                break;

            case KcpCommand.WindowProbeAsk:
                outgoingDatagrams.Add(CreateWindowProbeTellDatagram(nowMilliseconds));
                traceMessages.Add("received remote window probe ask");
                break;

            case KcpCommand.WindowProbeTell:
                traceMessages.Add($"received remote window probe tell wnd={segment.Window}");
                break;
            }
        }

        result = new MinimalKcpInboundResult(outgoingDatagrams, receivedPayloads, traceMessages);
        error = null;
        return true;
    }

    private void DrainReceivedPayloads(List<ReadOnlyMemory<byte>> payloads, List<string> traceMessages)
    {
        while (_receivedSegments.Remove(NextReceiveSequence, out KcpSegment segment))
        {
            payloads.Add(segment.Payload);
            traceMessages.Add($"delivered push sn={segment.SequenceNumber} bytes={segment.Payload.Length}");
            NextReceiveSequence++;
        }
    }

    private byte[] CreateAckDatagram(KcpSegment segment)
    {
        KcpSegment ackSegment = new(
            Conversation,
            KcpCommand.Ack,
            0,
            ClampWindow(_options.ReceiveWindow),
            segment.Timestamp,
            segment.SequenceNumber,
            NextReceiveSequence,
            ReadOnlyMemory<byte>.Empty);
        return KcpDatagramCodec.EncodeSegment(ackSegment);
    }

    private byte[] CreateWindowProbeTellDatagram(uint nowMilliseconds)
    {
        KcpSegment tellSegment = new(
            Conversation,
            KcpCommand.WindowProbeTell,
            0,
            ClampWindow(_options.ReceiveWindow),
            nowMilliseconds,
            0U,
            NextReceiveSequence,
            ReadOnlyMemory<byte>.Empty);
        return KcpDatagramCodec.EncodeSegment(tellSegment);
    }

    private static ushort ClampWindow(int value)
    {
        return value switch
        {
            <= 0 => 1,
            >= ushort.MaxValue => ushort.MaxValue,
            _ => (ushort)value,
        };
    }
}

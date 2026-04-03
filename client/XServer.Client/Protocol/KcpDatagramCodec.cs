using System.Buffers.Binary;

namespace XServer.Client.Protocol;

public static class KcpDatagramCodec
{
    public const int KcpOverheadBytes = 24;

    public static bool TryReadConversation(ReadOnlySpan<byte> datagram, out uint conversation)
    {
        conversation = 0;
        if (datagram.Length < 4)
        {
            return false;
        }

        conversation = BinaryPrimitives.ReadUInt32LittleEndian(datagram[..4]);
        return true;
    }

    public static bool TryDecodeDatagram(
        ReadOnlyMemory<byte> datagram,
        out IReadOnlyList<KcpSegment> segments,
        out string? error)
    {
        List<KcpSegment> parsedSegments = [];
        int offset = 0;

        while (offset < datagram.Length)
        {
            if (datagram.Length - offset < KcpOverheadBytes)
            {
                segments = Array.Empty<KcpSegment>();
                error = "KCP datagram does not contain enough bytes for a full segment header.";
                return false;
            }

            ReadOnlyMemory<byte> remaining = datagram[offset..];
            ReadOnlySpan<byte> header = remaining.Span[..KcpOverheadBytes];

            uint conversation = BinaryPrimitives.ReadUInt32LittleEndian(header[..4]);
            byte commandValue = header[4];
            byte fragment = header[5];
            ushort window = BinaryPrimitives.ReadUInt16LittleEndian(header.Slice(6, 2));
            uint timestamp = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(8, 4));
            uint sequenceNumber = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(12, 4));
            uint unacknowledgedSequenceNumber = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(16, 4));
            uint payloadLength = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(20, 4));

            if (!Enum.IsDefined(typeof(KcpCommand), commandValue))
            {
                segments = Array.Empty<KcpSegment>();
                error = $"KCP datagram contains an unsupported command byte '{commandValue}'.";
                return false;
            }

            if (payloadLength > int.MaxValue)
            {
                segments = Array.Empty<KcpSegment>();
                error = "KCP segment payload length exceeds the supported managed range.";
                return false;
            }

            int payloadLengthInt = checked((int)payloadLength);
            int segmentWireSize = KcpOverheadBytes + payloadLengthInt;
            if (datagram.Length - offset < segmentWireSize)
            {
                segments = Array.Empty<KcpSegment>();
                error = "KCP datagram payload length does not match the remaining datagram bytes.";
                return false;
            }

            ReadOnlyMemory<byte> payload = remaining.Slice(KcpOverheadBytes, payloadLengthInt);
            parsedSegments.Add(
                new KcpSegment(
                    conversation,
                    (KcpCommand)commandValue,
                    fragment,
                    window,
                    timestamp,
                    sequenceNumber,
                    unacknowledgedSequenceNumber,
                    payload));
            offset += segmentWireSize;
        }

        segments = parsedSegments;
        error = null;
        return true;
    }

    public static byte[] EncodeSegment(KcpSegment segment)
    {
        int wireSize = checked(KcpOverheadBytes + segment.Payload.Length);
        byte[] datagram = GC.AllocateUninitializedArray<byte>(wireSize);
        Span<byte> buffer = datagram;

        BinaryPrimitives.WriteUInt32LittleEndian(buffer[..4], segment.Conversation);
        buffer[4] = (byte)segment.Command;
        buffer[5] = segment.Fragment;
        BinaryPrimitives.WriteUInt16LittleEndian(buffer.Slice(6, 2), segment.Window);
        BinaryPrimitives.WriteUInt32LittleEndian(buffer.Slice(8, 4), segment.Timestamp);
        BinaryPrimitives.WriteUInt32LittleEndian(buffer.Slice(12, 4), segment.SequenceNumber);
        BinaryPrimitives.WriteUInt32LittleEndian(buffer.Slice(16, 4), segment.UnacknowledgedSequenceNumber);
        BinaryPrimitives.WriteUInt32LittleEndian(buffer.Slice(20, 4), checked((uint)segment.Payload.Length));
        segment.Payload.Span.CopyTo(buffer[KcpOverheadBytes..]);
        return datagram;
    }
}

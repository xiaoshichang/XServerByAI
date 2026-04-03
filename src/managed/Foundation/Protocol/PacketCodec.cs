using System.Buffers.Binary;

namespace XServer.Managed.Foundation.Protocol;

public enum PacketCodecErrorCode : byte
{
    None = 0,
    BufferTooSmall = 1,
    LengthOverflow = 2,
    InvalidMagic = 3,
    UnsupportedVersion = 4,
    InvalidFlags = 5,
    LengthMismatch = 6,
    InvalidArgument = 7,
}

public readonly record struct PacketView(PacketHeader Header, ReadOnlyMemory<byte> Payload);

public static class PacketCodec
{
    public static string GetErrorMessage(PacketCodecErrorCode errorCode)
    {
        return errorCode switch
        {
            PacketCodecErrorCode.None => "Success.",
            PacketCodecErrorCode.BufferTooSmall => "Buffer does not contain enough bytes for the requested packet operation.",
            PacketCodecErrorCode.LengthOverflow => "Packet payload length exceeds the supported range.",
            PacketCodecErrorCode.InvalidMagic => "Packet header magic does not match the protocol constant.",
            PacketCodecErrorCode.UnsupportedVersion => "Packet header version is not supported.",
            PacketCodecErrorCode.InvalidFlags => "Packet header contains undefined flag bits.",
            PacketCodecErrorCode.LengthMismatch => "Packet header length does not match the provided payload bytes.",
            PacketCodecErrorCode.InvalidArgument => "Packet codec argument must not be null.",
            _ => "Unknown packet codec error.",
        };
    }

    public static bool IsValidFlags(PacketFlags flags)
    {
        return ((ushort)flags & unchecked((ushort)~PacketConstants.DefinedFlagMask)) == 0;
    }

    public static PacketHeader CreateHeader(uint msgId, uint seq, PacketFlags flags, uint payloadLength)
    {
        return new PacketHeader
        {
            Magic = PacketConstants.Magic,
            Version = PacketConstants.Version,
            Flags = flags,
            Length = payloadLength,
            MsgId = msgId,
            Seq = seq,
        };
    }

    public static PacketCodecErrorCode ValidateHeader(PacketHeader header)
    {
        if (header.Magic != PacketConstants.Magic)
        {
            return PacketCodecErrorCode.InvalidMagic;
        }

        if (header.Version != PacketConstants.Version)
        {
            return PacketCodecErrorCode.UnsupportedVersion;
        }

        if (!IsValidFlags(header.Flags))
        {
            return PacketCodecErrorCode.InvalidFlags;
        }

        return PacketCodecErrorCode.None;
    }

    public static PacketCodecErrorCode GetWireSize(int payloadSize, out int wireSize)
    {
        wireSize = 0;

        if (payloadSize < 0)
        {
            return PacketCodecErrorCode.LengthOverflow;
        }

        if (payloadSize > int.MaxValue - PacketConstants.HeaderSize)
        {
            return PacketCodecErrorCode.LengthOverflow;
        }

        wireSize = PacketConstants.HeaderSize + payloadSize;
        return PacketCodecErrorCode.None;
    }

    public static PacketCodecErrorCode WriteHeader(PacketHeader header, Span<byte> buffer)
    {
        PacketCodecErrorCode validationResult = ValidateHeader(header);
        if (validationResult != PacketCodecErrorCode.None)
        {
            return validationResult;
        }

        if (buffer.Length < PacketConstants.HeaderSize)
        {
            return PacketCodecErrorCode.BufferTooSmall;
        }

        BinaryPrimitives.WriteUInt32BigEndian(buffer[..4], header.Magic);
        BinaryPrimitives.WriteUInt16BigEndian(buffer.Slice(4, 2), header.Version);
        BinaryPrimitives.WriteUInt16BigEndian(buffer.Slice(6, 2), (ushort)header.Flags);
        BinaryPrimitives.WriteUInt32BigEndian(buffer.Slice(8, 4), header.Length);
        BinaryPrimitives.WriteUInt32BigEndian(buffer.Slice(12, 4), header.MsgId);
        BinaryPrimitives.WriteUInt32BigEndian(buffer.Slice(16, 4), header.Seq);
        return PacketCodecErrorCode.None;
    }

    public static PacketCodecErrorCode ReadHeader(ReadOnlySpan<byte> buffer, out PacketHeader header)
    {
        header = default;
        if (buffer.Length < PacketConstants.HeaderSize)
        {
            return PacketCodecErrorCode.BufferTooSmall;
        }

        PacketHeader parsedHeader = new()
        {
            Magic = BinaryPrimitives.ReadUInt32BigEndian(buffer[..4]),
            Version = BinaryPrimitives.ReadUInt16BigEndian(buffer.Slice(4, 2)),
            Flags = (PacketFlags)BinaryPrimitives.ReadUInt16BigEndian(buffer.Slice(6, 2)),
            Length = BinaryPrimitives.ReadUInt32BigEndian(buffer.Slice(8, 4)),
            MsgId = BinaryPrimitives.ReadUInt32BigEndian(buffer.Slice(12, 4)),
            Seq = BinaryPrimitives.ReadUInt32BigEndian(buffer.Slice(16, 4)),
        };

        PacketCodecErrorCode validationResult = ValidateHeader(parsedHeader);
        if (validationResult != PacketCodecErrorCode.None)
        {
            return validationResult;
        }

        header = parsedHeader;
        return PacketCodecErrorCode.None;
    }

    public static PacketCodecErrorCode EncodePacket(
        PacketHeader header,
        ReadOnlySpan<byte> payload,
        Span<byte> buffer)
    {
        PacketCodecErrorCode validationResult = ValidateHeader(header);
        if (validationResult != PacketCodecErrorCode.None)
        {
            return validationResult;
        }

        if (header.Length != (uint)payload.Length)
        {
            return PacketCodecErrorCode.LengthMismatch;
        }

        PacketCodecErrorCode wireSizeResult = GetWireSize(payload.Length, out int wireSize);
        if (wireSizeResult != PacketCodecErrorCode.None)
        {
            return wireSizeResult;
        }

        if (buffer.Length < wireSize)
        {
            return PacketCodecErrorCode.BufferTooSmall;
        }

        PacketCodecErrorCode headerResult = WriteHeader(header, buffer[..PacketConstants.HeaderSize]);
        if (headerResult != PacketCodecErrorCode.None)
        {
            return headerResult;
        }

        if (!payload.IsEmpty)
        {
            payload.CopyTo(buffer[PacketConstants.HeaderSize..wireSize]);
        }

        return PacketCodecErrorCode.None;
    }

    public static PacketCodecErrorCode DecodePacket(ReadOnlyMemory<byte> buffer, out PacketView packet)
    {
        packet = default;

        PacketCodecErrorCode headerResult = ReadHeader(buffer.Span, out PacketHeader header);
        if (headerResult != PacketCodecErrorCode.None)
        {
            return headerResult;
        }

        if (header.Length > int.MaxValue)
        {
            return PacketCodecErrorCode.LengthOverflow;
        }

        PacketCodecErrorCode wireSizeResult = GetWireSize((int)header.Length, out int wireSize);
        if (wireSizeResult != PacketCodecErrorCode.None)
        {
            return wireSizeResult;
        }

        if (buffer.Length != wireSize)
        {
            return PacketCodecErrorCode.LengthMismatch;
        }

        packet = new PacketView(header, buffer.Slice(PacketConstants.HeaderSize, (int)header.Length));
        return PacketCodecErrorCode.None;
    }
}

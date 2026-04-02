using System;
using System.Buffers.Binary;
using System.Text;

namespace XServer.Managed.Framework.Interop
{
    internal static class RelayStubCallCodec
    {
        public const uint ForwardStubCallMsgId = 2002u;

        internal readonly record struct RelayStubCallEnvelope(
            string SourceGameNodeId,
            string TargetGameNodeId,
            string TargetStubType,
            uint StubCallMsgId,
            ReadOnlyMemory<byte> Payload);

        public static unsafe bool TryDecode(
            byte* payload,
            uint payloadLength,
            out RelayStubCallEnvelope message)
        {
            if (payload == null)
            {
                message = default;
                return false;
            }

            return TryDecode(new ReadOnlySpan<byte>(payload, checked((int)payloadLength)), out message);
        }

        public static bool TryDecode(
            ReadOnlySpan<byte> payload,
            out RelayStubCallEnvelope message)
        {
            int offset = 0;
            return TryDecode(payload, ref offset, out message);
        }

        private static bool TryDecode(
            ReadOnlySpan<byte> payload,
            ref int offset,
            out RelayStubCallEnvelope message)
        {
            message = default;

            if (!TryReadString16(payload, ref offset, out string sourceGameNodeId) ||
                !TryReadString16(payload, ref offset, out string targetGameNodeId) ||
                !TryReadString16(payload, ref offset, out string targetStubType) ||
                !TryReadUInt32(payload, ref offset, out uint stubCallMsgId) ||
                !TryReadUInt32(payload, ref offset, out uint relayFlags) ||
                !TryReadBytes32(payload, ref offset, out byte[] callPayload))
            {
                return false;
            }

            if (offset != payload.Length ||
                string.IsNullOrWhiteSpace(sourceGameNodeId) ||
                string.IsNullOrWhiteSpace(targetGameNodeId) ||
                string.IsNullOrWhiteSpace(targetStubType) ||
                stubCallMsgId == 0 ||
                relayFlags != 0)
            {
                return false;
            }

            message = new RelayStubCallEnvelope(
                sourceGameNodeId,
                targetGameNodeId,
                targetStubType,
                stubCallMsgId,
                callPayload);
            return true;
        }

        private static bool TryReadString16(
            ReadOnlySpan<byte> buffer,
            ref int offset,
            out string value)
        {
            value = string.Empty;

            if (!TryReadUInt16(buffer, ref offset, out ushort byteCount))
            {
                return false;
            }

            if (byteCount > buffer.Length - offset)
            {
                return false;
            }

            value = Encoding.UTF8.GetString(buffer.Slice(offset, byteCount));
            offset += byteCount;
            return true;
        }

        private static bool TryReadBytes32(
            ReadOnlySpan<byte> buffer,
            ref int offset,
            out byte[] value)
        {
            value = Array.Empty<byte>();

            if (!TryReadUInt32(buffer, ref offset, out uint byteCount))
            {
                return false;
            }

            if (byteCount > (uint)(buffer.Length - offset))
            {
                return false;
            }

            value = buffer.Slice(offset, checked((int)byteCount)).ToArray();
            offset += checked((int)byteCount);
            return true;
        }

        private static bool TryReadUInt16(
            ReadOnlySpan<byte> buffer,
            ref int offset,
            out ushort value)
        {
            value = 0;
            if (sizeof(ushort) > buffer.Length - offset)
            {
                return false;
            }

            value = BinaryPrimitives.ReadUInt16BigEndian(buffer.Slice(offset, sizeof(ushort)));
            offset += sizeof(ushort);
            return true;
        }

        private static bool TryReadUInt32(
            ReadOnlySpan<byte> buffer,
            ref int offset,
            out uint value)
        {
            value = 0;
            if (sizeof(uint) > buffer.Length - offset)
            {
                return false;
            }

            value = BinaryPrimitives.ReadUInt32BigEndian(buffer.Slice(offset, sizeof(uint)));
            offset += sizeof(uint);
            return true;
        }
    }
}

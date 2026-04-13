using System;
using System.Buffers.Binary;
using System.Text;
using XServer.Managed.Framework.Protocol;

namespace XServer.Managed.Framework.Interop
{
    internal static class RelayProxyCallCodec
    {
        public const uint ForwardProxyCallMsgId = ServerInternalMessageIds.msgid_game_game_forwardproxycall;

        internal readonly record struct RelayProxyCallEnvelope(
            string SourceGameNodeId,
            string RouteGateNodeId,
            Guid TargetEntityId,
            uint ProxyCallMsgId,
            ReadOnlyMemory<byte> Payload);

        public static byte[] Encode(
            string sourceGameNodeId,
            string routeGateNodeId,
            Guid targetEntityId,
            uint proxyCallMsgId,
            ReadOnlyMemory<byte> payload)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(sourceGameNodeId);
            ArgumentException.ThrowIfNullOrWhiteSpace(routeGateNodeId);
            ArgumentOutOfRangeException.ThrowIfZero(proxyCallMsgId);
            if (targetEntityId == Guid.Empty)
            {
                throw new ArgumentException("Relay proxy target entityId must not be empty.", nameof(targetEntityId));
            }

            byte[] sourceGameNodeIdUtf8 = Encoding.UTF8.GetBytes(sourceGameNodeId);
            byte[] routeGateNodeIdUtf8 = Encoding.UTF8.GetBytes(routeGateNodeId);
            byte[] targetEntityIdUtf8 = Encoding.UTF8.GetBytes(targetEntityId.ToString("D"));
            byte[] callPayload = payload.ToArray();
            byte[] buffer = new byte[
                sizeof(ushort) + sourceGameNodeIdUtf8.Length +
                sizeof(ushort) + routeGateNodeIdUtf8.Length +
                sizeof(ushort) + targetEntityIdUtf8.Length +
                sizeof(uint) +
                sizeof(uint) +
                sizeof(uint) + callPayload.Length];

            int offset = 0;
            WriteString16(buffer, ref offset, sourceGameNodeIdUtf8);
            WriteString16(buffer, ref offset, routeGateNodeIdUtf8);
            WriteString16(buffer, ref offset, targetEntityIdUtf8);
            WriteUInt32(buffer, ref offset, proxyCallMsgId);
            WriteUInt32(buffer, ref offset, 0u);
            WriteBytes32(buffer, ref offset, callPayload);
            return buffer;
        }

        public static unsafe bool TryDecode(
            byte* payload,
            uint payloadLength,
            out RelayProxyCallEnvelope message)
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
            out RelayProxyCallEnvelope message)
        {
            int offset = 0;
            message = default;

            if (!TryReadString16(payload, ref offset, out string sourceGameNodeId) ||
                !TryReadString16(payload, ref offset, out string routeGateNodeId) ||
                !TryReadString16(payload, ref offset, out string targetEntityIdText) ||
                !TryReadUInt32(payload, ref offset, out uint proxyCallMsgId) ||
                !TryReadUInt32(payload, ref offset, out uint relayFlags) ||
                !TryReadBytes32(payload, ref offset, out byte[] callPayload))
            {
                return false;
            }

            if (offset != payload.Length ||
                string.IsNullOrWhiteSpace(sourceGameNodeId) ||
                string.IsNullOrWhiteSpace(routeGateNodeId) ||
                proxyCallMsgId == 0 ||
                relayFlags != 0 ||
                !Guid.TryParse(targetEntityIdText, out Guid targetEntityId) ||
                targetEntityId == Guid.Empty)
            {
                return false;
            }

            message = new RelayProxyCallEnvelope(
                sourceGameNodeId,
                routeGateNodeId,
                targetEntityId,
                proxyCallMsgId,
                callPayload);
            return true;
        }

        private static void WriteString16(byte[] buffer, ref int offset, byte[] value)
        {
            BinaryPrimitives.WriteUInt16BigEndian(buffer.AsSpan(offset, sizeof(ushort)), checked((ushort)value.Length));
            offset += sizeof(ushort);
            value.CopyTo(buffer, offset);
            offset += value.Length;
        }

        private static void WriteUInt32(byte[] buffer, ref int offset, uint value)
        {
            BinaryPrimitives.WriteUInt32BigEndian(buffer.AsSpan(offset, sizeof(uint)), value);
            offset += sizeof(uint);
        }

        private static void WriteBytes32(byte[] buffer, ref int offset, byte[] value)
        {
            WriteUInt32(buffer, ref offset, checked((uint)value.Length));
            value.CopyTo(buffer, offset);
            offset += value.Length;
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

using System.Runtime.InteropServices;

namespace XServer.Managed.Foundation.Protocol;

[Flags]
public enum PacketFlags : ushort
{
    None = 0,
    Response = 1 << 0,
    Compressed = 1 << 1,
    Error = 1 << 2,
}

public static class PacketConstants
{
    public const uint Magic = 0x47535052;
    public const ushort Version = 1;
    public const int HeaderSize = 20;
    public const ushort DefinedFlagMask = (ushort)(PacketFlags.Response | PacketFlags.Compressed | PacketFlags.Error);
    public const uint SeqNone = 0;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct PacketHeader
{
    public uint Magic;
    public ushort Version;
    public PacketFlags Flags;
    public uint Length;
    public uint MsgId;
    public uint Seq;
}

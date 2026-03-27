using System.Runtime.InteropServices;

namespace XServer.Managed.GameLogic.Interop
{
    public static class ManagedAbi
    {
        public const uint Version = 1;
        public const int ServerStubEntityTypeMaxUtf8Bytes = 128;
        public const int ServerStubEntityIdMaxUtf8Bytes = 128;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ManagedInitArgs
    {
        public uint StructSize;
        public uint AbiVersion;
        public ushort ProcessType;
        public ushort Reserved0;
        public byte* NodeIdUtf8;
        public uint NodeIdLength;
        public byte* ConfigPathUtf8;
        public uint ConfigPathLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ManagedMessageView
    {
        public uint StructSize;
        public uint MsgId;
        public uint Seq;
        public uint Flags;
        public ulong SessionId;
        public ulong PlayerId;
        public byte* Payload;
        public uint PayloadLength;
        public uint Reserved0;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ManagedServerStubCatalogEntry
    {
        public uint StructSize;
        public uint EntityTypeLength;
        public fixed byte EntityTypeUtf8[ManagedAbi.ServerStubEntityTypeMaxUtf8Bytes];
        public uint EntityIdLength;
        public fixed byte EntityIdUtf8[ManagedAbi.ServerStubEntityIdMaxUtf8Bytes];
        public uint Reserved0;
    }
}

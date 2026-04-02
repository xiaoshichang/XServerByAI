using System.Runtime.InteropServices;

namespace XServer.Managed.Framework.Interop
{
    public static class ManagedAbi
    {
        public const uint Version = 6;
        public const int NodeIdMaxUtf8Bytes = 128;
        public const int ServerStubEntityTypeMaxUtf8Bytes = 128;
        public const int ServerStubEntityIdMaxUtf8Bytes = 128;
    }

    public enum ManagedLogLevel : uint
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Fatal = 5,
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ManagedNativeCallbacks
    {
        public uint StructSize;
        public uint Reserved0;
        public void* Context;
        public delegate* unmanaged[Cdecl]<void*, ulong, ManagedServerStubReadyEntry*, void> OnServerStubReady;
        public delegate* unmanaged[Cdecl]<void*, uint, byte*, uint, byte*, uint, void> OnLog;
        public delegate* unmanaged[Cdecl]<void*, ulong, long> CreateOnceTimer;
        public delegate* unmanaged[Cdecl]<void*, long, int> CancelTimer;
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
        public ManagedNativeCallbacks NativeCallbacks;
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

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ManagedServerStubOwnershipEntry
    {
        public uint StructSize;
        public uint EntityTypeLength;
        public fixed byte EntityTypeUtf8[ManagedAbi.ServerStubEntityTypeMaxUtf8Bytes];
        public uint EntityIdLength;
        public fixed byte EntityIdUtf8[ManagedAbi.ServerStubEntityIdMaxUtf8Bytes];
        public uint OwnerGameNodeIdLength;
        public fixed byte OwnerGameNodeIdUtf8[ManagedAbi.NodeIdMaxUtf8Bytes];
        public uint EntryFlags;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ManagedServerStubOwnershipSync
    {
        public uint StructSize;
        public uint StatusFlags;
        public ulong AssignmentEpoch;
        public ulong ServerNowUnixMs;
        public uint AssignmentCount;
        public uint Reserved0;
        public ManagedServerStubOwnershipEntry* Assignments;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ManagedServerStubReadyEntry
    {
        public uint StructSize;
        public uint EntityTypeLength;
        public fixed byte EntityTypeUtf8[ManagedAbi.ServerStubEntityTypeMaxUtf8Bytes];
        public uint EntityIdLength;
        public fixed byte EntityIdUtf8[ManagedAbi.ServerStubEntityIdMaxUtf8Bytes];
        public byte Ready;
        public fixed byte Reserved0[3];
        public uint EntryFlags;
    }
}

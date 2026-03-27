using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using XServer.Managed.GameLogic.Catalog;

namespace XServer.Managed.GameLogic.Interop
{
    public static unsafe class GameNativeExports
    {
        private const int CatalogInvalidArgument = -1;
        private const int CatalogIndexOutOfRange = -2;
        private const int CatalogBufferTooSmall = -3;

        [UnmanagedCallersOnly(EntryPoint = "GameNativeGetAbiVersion", CallConvs = [typeof(CallConvCdecl)])]
        public static uint GameNativeGetAbiVersion()
        {
            return ManagedAbi.Version;
        }

        [UnmanagedCallersOnly(EntryPoint = "GameNativeInit", CallConvs = [typeof(CallConvCdecl)])]
        public static int GameNativeInit(ManagedInitArgs* args)
        {
            _ = args;
            return 0;
        }

        [UnmanagedCallersOnly(EntryPoint = "GameNativeOnMessage", CallConvs = [typeof(CallConvCdecl)])]
        public static int GameNativeOnMessage(ManagedMessageView* message)
        {
            _ = message;
            return 0;
        }

        [UnmanagedCallersOnly(EntryPoint = "GameNativeOnTick", CallConvs = [typeof(CallConvCdecl)])]
        public static int GameNativeOnTick(ulong nowUnixMsUtc, uint deltaMs)
        {
            _ = nowUnixMsUtc;
            _ = deltaMs;
            return 0;
        }

        [UnmanagedCallersOnly(EntryPoint = "GameNativeGetServerStubCatalogCount", CallConvs = [typeof(CallConvCdecl)])]
        public static int GameNativeGetServerStubCatalogCount(uint* count)
        {
            if (count == null)
            {
                return CatalogInvalidArgument;
            }

            try
            {
                *count = checked((uint)ServerStubCatalog.Entries.Count);
                return 0;
            }
            catch
            {
                *count = 0;
                return CatalogInvalidArgument;
            }
        }

        [UnmanagedCallersOnly(EntryPoint = "GameNativeGetServerStubCatalogEntry", CallConvs = [typeof(CallConvCdecl)])]
        public static int GameNativeGetServerStubCatalogEntry(uint index, ManagedServerStubCatalogEntry* entry)
        {
            if (entry == null || entry->StructSize < sizeof(ManagedServerStubCatalogEntry))
            {
                return CatalogInvalidArgument;
            }

            try
            {
                if (index >= (uint)ServerStubCatalog.Entries.Count)
                {
                    ResetCatalogEntry(entry);
                    return CatalogIndexOutOfRange;
                }

                ServerStubCatalogEntry catalogEntry = ServerStubCatalog.Entries[(int)index];
                ResetCatalogEntry(entry);

                int entityTypeResult = CopyUtf8(
                    catalogEntry.EntityTypeUtf8,
                    entry->EntityTypeUtf8,
                    ManagedAbi.ServerStubEntityTypeMaxUtf8Bytes,
                    &entry->EntityTypeLength);
                if (entityTypeResult != 0)
                {
                    return entityTypeResult;
                }

                int entityIdResult = CopyUtf8(
                    catalogEntry.EntityIdUtf8,
                    entry->EntityIdUtf8,
                    ManagedAbi.ServerStubEntityIdMaxUtf8Bytes,
                    &entry->EntityIdLength);
                if (entityIdResult != 0)
                {
                    return entityIdResult;
                }

                return 0;
            }
            catch
            {
                ResetCatalogEntry(entry);
                return CatalogInvalidArgument;
            }
        }

        private static void ResetCatalogEntry(ManagedServerStubCatalogEntry* entry)
        {
            entry->StructSize = (uint)sizeof(ManagedServerStubCatalogEntry);
            entry->EntityTypeLength = 0;
            entry->EntityIdLength = 0;
            entry->Reserved0 = 0;

            for (int index = 0; index < ManagedAbi.ServerStubEntityTypeMaxUtf8Bytes; ++index)
            {
                entry->EntityTypeUtf8[index] = 0;
            }

            for (int index = 0; index < ManagedAbi.ServerStubEntityIdMaxUtf8Bytes; ++index)
            {
                entry->EntityIdUtf8[index] = 0;
            }
        }

        private static int CopyUtf8(byte[] source, byte* destination, int capacity, uint* outputLength)
        {
            if (outputLength == null)
            {
                return CatalogInvalidArgument;
            }

            if (source.Length > capacity)
            {
                *outputLength = 0;
                return CatalogBufferTooSmall;
            }

            for (int index = 0; index < source.Length; ++index)
            {
                destination[index] = source[index];
            }

            for (int index = source.Length; index < capacity; ++index)
            {
                destination[index] = 0;
            }

            *outputLength = (uint)source.Length;
            return 0;
        }
    }
}

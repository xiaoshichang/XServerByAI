using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace XServer.Managed.Framework.Interop
{
    public static unsafe class GameNativeExports
    {
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
    }
}
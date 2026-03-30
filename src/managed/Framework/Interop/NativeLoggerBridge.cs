using System.Text;

namespace XServer.Managed.Framework.Interop
{
    public static unsafe class NativeLoggerBridge
    {
        private const string DefaultCategory = "managed.runtime";
        private static ManagedNativeCallbacks s_nativeCallbacks;

        public static void Configure(ManagedNativeCallbacks nativeCallbacks)
        {
            s_nativeCallbacks = nativeCallbacks;
        }

        public static void Reset()
        {
            s_nativeCallbacks = default;
        }

        public static void Trace(string category, string message)
        {
            Log(ManagedLogLevel.Trace, category, message);
        }

        public static void Debug(string category, string message)
        {
            Log(ManagedLogLevel.Debug, category, message);
        }

        public static void Info(string category, string message)
        {
            Log(ManagedLogLevel.Info, category, message);
        }

        public static void Warn(string category, string message)
        {
            Log(ManagedLogLevel.Warn, category, message);
        }

        public static void Error(string category, string message)
        {
            Log(ManagedLogLevel.Error, category, message);
        }

        public static void Fatal(string category, string message)
        {
            Log(ManagedLogLevel.Fatal, category, message);
        }

        public static void Log(ManagedLogLevel level, string? category, string? message)
        {
            if (s_nativeCallbacks.OnLog == null)
            {
                return;
            }

            try
            {
                string resolvedCategory = string.IsNullOrEmpty(category) ? DefaultCategory : category;
                string resolvedMessage = message ?? string.Empty;
                byte[] categoryUtf8 = Encoding.UTF8.GetBytes(resolvedCategory);
                byte[] messageUtf8 = Encoding.UTF8.GetBytes(resolvedMessage);

                fixed (byte* categoryPtr = categoryUtf8)
                fixed (byte* messagePtr = messageUtf8)
                {
                    s_nativeCallbacks.OnLog(
                        s_nativeCallbacks.Context,
                        (uint)level,
                        categoryUtf8.Length == 0 ? null : categoryPtr,
                        checked((uint)categoryUtf8.Length),
                        messageUtf8.Length == 0 ? null : messagePtr,
                        checked((uint)messageUtf8.Length));
                }
            }
            catch
            {
            }
        }
    }
}
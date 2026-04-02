using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Interop
{
    internal sealed unsafe class ManagedNativeTimerScheduler : INativeTimerScheduler
    {
        private readonly object _syncRoot = new();
        private readonly Dictionary<long, Action> _onceCallbacks = [];
        private ManagedNativeCallbacks _nativeCallbacks;

        public ManagedNativeTimerScheduler(ManagedNativeCallbacks nativeCallbacks)
        {
            _nativeCallbacks = nativeCallbacks;
        }

        public long CreateOnce(TimeSpan delay, Action callback)
        {
            if (callback == null)
            {
                return (long)NativeTimerErrorCode.CallbackEmpty;
            }

            if (_nativeCallbacks.CreateOnceTimer == null)
            {
                return (long)NativeTimerErrorCode.Unknown;
            }

            ulong delayMs = checked((ulong)Math.Max(0, (long)delay.TotalMilliseconds));
            long nativeTimerId = _nativeCallbacks.CreateOnceTimer(_nativeCallbacks.Context, delayMs);
            if (!NativeTimerResult.IsTimerId(nativeTimerId))
            {
                return nativeTimerId;
            }

            lock (_syncRoot)
            {
                _onceCallbacks.Add(nativeTimerId, callback);
            }

            return nativeTimerId;
        }

        public bool Cancel(long timerId)
        {
            if (timerId <= 0 || _nativeCallbacks.CancelTimer == null)
            {
                return false;
            }

            int result = _nativeCallbacks.CancelTimer(_nativeCallbacks.Context, timerId);
            if (result != 0)
            {
                return false;
            }

            lock (_syncRoot)
            {
                _onceCallbacks.Remove(timerId);
            }

            return true;
        }

        public int HandleTimerFired(long timerId)
        {
            Action? callback;
            lock (_syncRoot)
            {
                if (!_onceCallbacks.Remove(timerId, out callback) || callback == null)
                {
                    return -1;
                }
            }

            try
            {
                callback();
                return 0;
            }
            catch
            {
                return -1;
            }
        }

        public void Reset()
        {
            lock (_syncRoot)
            {
                _onceCallbacks.Clear();
            }

            _nativeCallbacks = default;
        }
    }
}

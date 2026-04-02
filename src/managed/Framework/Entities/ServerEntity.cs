using System;
using XServer.Managed.Framework.Interop;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Entities
{
    public abstract partial class ServerEntity
    {
        private IServerStubCaller? _stubCaller;
        private INativeTimerScheduler? _nativeTimerScheduler;

        protected ServerEntity()
        {
            EntityId = Guid.NewGuid();
            LifecycleState = EntityLifecycleState.Constructed;
        }

        [EntityProperty(EntityPropertyFlags.AllClients | EntityPropertyFlags.Persistent)]
        protected Guid __EntityId;

        public string EntityType => GetType().Name;

        public virtual bool IsMigratable()
        {
            return true;
        }

        public void CallStub(string targetStubType, uint msgId, ReadOnlyMemory<byte> payload)
        {
            if (string.IsNullOrWhiteSpace(targetStubType))
            {
                LogCallStubError("Target stub type must not be empty.");
                return;
            }

            if (msgId == 0)
            {
                LogCallStubError("Stub call msgId must not be zero.");
                return;
            }

            if (_stubCaller == null)
            {
                LogCallStubError("Stub caller is not configured for this entity.");
                return;
            }

            _stubCaller.CallStub(this, targetStubType, new StubCallMessage(msgId, payload));
        }

        internal void SetStubCaller(IServerStubCaller? stubCaller)
        {
            _stubCaller = stubCaller;
        }

        internal void SetNativeTimerScheduler(INativeTimerScheduler? nativeTimerScheduler)
        {
            _nativeTimerScheduler = nativeTimerScheduler;
        }

        protected long CreateNativeOnceTimer(TimeSpan delay, Action callback)
        {
            if (_nativeTimerScheduler == null)
            {
                return (long)NativeTimerErrorCode.Unknown;
            }

            return _nativeTimerScheduler.CreateOnce(delay, callback);
        }

        protected bool CancelNativeTimer(long timerId)
        {
            if (_nativeTimerScheduler == null)
            {
                return false;
            }

            return _nativeTimerScheduler.Cancel(timerId);
        }

        private void LogCallStubError(string message)
        {
            NativeLoggerBridge.Warn(EntityType, message);
        }
    }
}

using System;
using XServer.Managed.Framework.Interop;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Entities
{
    public abstract partial class ServerEntity
    {
        private IServerStubCaller? _stubCaller;
        private IProxyEntityCaller? _proxyEntityCaller;
        private IClientMessageSender? _clientMessageSender;
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

        public void CallProxy(ProxyAddress targetAddress, uint msgId, ReadOnlyMemory<byte> payload)
        {
            if (targetAddress == null ||
                targetAddress.EntityId == Guid.Empty ||
                string.IsNullOrWhiteSpace(targetAddress.RouteGateNodeId))
            {
                LogCallProxyError("Proxy address is invalid.");
                return;
            }

            if (msgId == 0)
            {
                LogCallProxyError("Proxy call msgId must not be zero.");
                return;
            }

            if (_proxyEntityCaller == null)
            {
                LogCallProxyError("Proxy caller is not configured for this entity.");
                return;
            }

            _proxyEntityCaller.CallProxy(this, targetAddress, new ProxyCallMessage(msgId, payload));
        }

        internal void SetStubCaller(IServerStubCaller? stubCaller)
        {
            _stubCaller = stubCaller;
        }

        internal void SetProxyEntityCaller(IProxyEntityCaller? proxyEntityCaller)
        {
            _proxyEntityCaller = proxyEntityCaller;
        }

        internal void SetClientMessageSender(IClientMessageSender? clientMessageSender)
        {
            _clientMessageSender = clientMessageSender;
        }

        internal void SetNativeTimerScheduler(INativeTimerScheduler? nativeTimerScheduler)
        {
            _nativeTimerScheduler = nativeTimerScheduler;
        }

        internal ProxyCallErrorCode ReceiveProxyCall(ProxyCallMessage message)
        {
            if (message.MsgId == 0)
            {
                return ProxyCallErrorCode.InvalidMessageId;
            }

            return OnProxyCall(message);
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

        protected virtual ProxyCallErrorCode OnProxyCall(ProxyCallMessage message)
        {
            _ = message;
            return ProxyCallErrorCode.EntityRejected;
        }

        protected void PushToClient(ProxyAddress targetAddress, uint msgId, ReadOnlyMemory<byte> payload)
        {
            if (targetAddress == null ||
                targetAddress.EntityId == Guid.Empty ||
                string.IsNullOrWhiteSpace(targetAddress.RouteGateNodeId))
            {
                LogPushToClientError("Client target address is invalid.");
                return;
            }

            if (msgId == 0)
            {
                LogPushToClientError("Client push msgId must not be zero.");
                return;
            }

            if (_clientMessageSender == null)
            {
                LogPushToClientError("Client message sender is not configured for this entity.");
                return;
            }

            _clientMessageSender.PushToClient(this, targetAddress, new ProxyCallMessage(msgId, payload));
        }

        private void LogCallStubError(string message)
        {
            NativeLoggerBridge.Warn(EntityType, message);
        }

        private void LogCallProxyError(string message)
        {
            NativeLoggerBridge.Warn(EntityType, message);
        }

        private void LogPushToClientError(string message)
        {
            NativeLoggerBridge.Warn(EntityType, message);
        }
    }
}

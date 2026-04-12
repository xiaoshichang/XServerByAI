using System;
using XServer.Managed.Foundation.Rpc;
using XServer.Managed.Framework.Interop;
using XServer.Managed.Framework.Rpc;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Entities
{
    public abstract partial class ServerEntity
    {
        private IServerEntityMessageSender? _messageSender;
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

            if (_messageSender == null)
            {
                LogCallStubError("Stub caller is not configured for this entity.");
                return;
            }

            _messageSender.CallStub(this, targetStubType, new EntityMessage(msgId, payload));
        }

        public void CallMailbox(MailboxAddress targetAddress, uint msgId, ReadOnlyMemory<byte> payload)
        {
            if (targetAddress == null ||
                targetAddress.EntityId == Guid.Empty ||
                string.IsNullOrWhiteSpace(targetAddress.TargetGameNodeId))
            {
                LogCallMailboxError("Mailbox address is invalid.");
                return;
            }

            if (msgId == 0)
            {
                LogCallMailboxError("Mailbox call msgId must not be zero.");
                return;
            }

            if (_messageSender == null)
            {
                LogCallMailboxError("Mailbox caller is not configured for this entity.");
                return;
            }

            _messageSender.CallMailbox(this, targetAddress, new EntityMessage(msgId, payload));
        }

        public void CallProxy(ProxyAddress targetAddress, uint msgId, ReadOnlyMemory<byte> payload)
        {
            CallServerProxy(targetAddress, msgId, payload);
        }

        public void CallServerProxy(ProxyAddress targetAddress, uint msgId, ReadOnlyMemory<byte> payload)
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

            if (_messageSender == null)
            {
                LogCallProxyError("Proxy caller is not configured for this entity.");
                return;
            }

            _messageSender.CallServerProxy(this, targetAddress, new EntityMessage(msgId, payload));
        }

        public void CallClientRpc(string rpcName, params object?[] arguments)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(rpcName);
            ArgumentNullException.ThrowIfNull(arguments);

            ProxyAddress? targetAddress = GetDefaultClientRpcTargetAddress();
            if (targetAddress == null)
            {
                NativeLoggerBridge.Warn(EntityType, $"CallClientRpc rpcName={rpcName} failed: default client proxy is unavailable.");
                return;
            }

            byte[] payload;
            try
            {
                payload = EntityRpcJsonCodec.Encode(EntityId, rpcName, arguments);
            }
            catch (ArgumentException exception)
            {
                NativeLoggerBridge.Warn(EntityType, $"CallClientRpc rpcName={rpcName} failed: {exception.Message}");
                return;
            }

            PushToClient(targetAddress, EntityRpcMessageIds.ServerToClientEntityRpcMsgId, payload);
        }

        public void CallClientRPC(string rpcName, params object?[] arguments)
        {
            CallClientRpc(rpcName, arguments);
        }

        internal void SetMessageSender(IServerEntityMessageSender? messageSender)
        {
            _messageSender = messageSender;
        }

        internal void SetNativeTimerScheduler(INativeTimerScheduler? nativeTimerScheduler)
        {
            _nativeTimerScheduler = nativeTimerScheduler;
        }

        internal ProxyCallErrorCode ReceiveProxyCall(EntityMessage message)
        {
            if (message.MsgId == 0)
            {
                return ProxyCallErrorCode.InvalidMessageId;
            }

            if (message.MsgId == EntityRpcMessageIds.ClientToServerEntityRpcMsgId)
            {
                return ReceiveClientRpc(message.Payload);
            }

            return OnProxyCall(message);
        }

        internal MailboxCallErrorCode ReceiveMailboxCall(EntityMessage message)
        {
            if (message.MsgId == 0)
            {
                return MailboxCallErrorCode.InvalidMessageId;
            }

            if (IsMigratable())
            {
                return MailboxCallErrorCode.MailboxRejected;
            }

            return OnMailboxCall(message);
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

        protected virtual ProxyCallErrorCode OnProxyCall(EntityMessage message)
        {
            _ = message;
            return ProxyCallErrorCode.EntityRejected;
        }

        protected virtual MailboxCallErrorCode OnMailboxCall(EntityMessage message)
        {
            _ = message;
            return MailboxCallErrorCode.MailboxRejected;
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

            if (_messageSender == null)
            {
                LogPushToClientError("Client message sender is not configured for this entity.");
                return;
            }

            _messageSender.CallClient(this, targetAddress, new EntityMessage(msgId, payload));
        }

        protected virtual ProxyAddress? GetDefaultClientRpcTargetAddress()
        {
            return null;
        }

        private void LogCallStubError(string message)
        {
            NativeLoggerBridge.Warn(EntityType, message);
        }

        private void LogCallProxyError(string message)
        {
            NativeLoggerBridge.Warn(EntityType, message);
        }

        private void LogCallMailboxError(string message)
        {
            NativeLoggerBridge.Warn(EntityType, message);
        }

        private void LogPushToClientError(string message)
        {
            NativeLoggerBridge.Warn(EntityType, message);
        }

        private ProxyCallErrorCode ReceiveClientRpc(ReadOnlyMemory<byte> payload)
        {
            EntityRpcDispatchErrorCode result = ServerEntityRpcDispatcher.Dispatch(
                this,
                payload,
                out string rpcName,
                out string errorMessage);
            if (result != EntityRpcDispatchErrorCode.None)
            {
                NativeLoggerBridge.Warn(
                    EntityType,
                    $"ReceiveClientRpc rpcName={rpcName} entityId={EntityId:D} failed: {errorMessage}");
                return result is EntityRpcDispatchErrorCode.InvalidPayload or
                    EntityRpcDispatchErrorCode.InvalidArgumentCount or
                    EntityRpcDispatchErrorCode.InvalidArgumentType
                    ? ProxyCallErrorCode.InvalidArgument
                    : ProxyCallErrorCode.EntityRejected;
            }

            NativeLoggerBridge.Info(EntityType, $"ReceiveClientRpc rpcName={rpcName} entityId={EntityId:D} succeeded.");
            return ProxyCallErrorCode.None;
        }
    }
}

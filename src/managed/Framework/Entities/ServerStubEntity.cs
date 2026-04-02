using System;
using XServer.Managed.Framework.Interop;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Entities
{
    // Stub entities represent shared services whose owner game node is assigned by GM during startup.
    public abstract partial class ServerStubEntity : ServerEntity
    {
        private Action<ServerStubEntity>? _readyCallback;

        public bool IsReady { get; private set; }

        public override bool IsMigratable()
        {
            return false;
        }

        public bool TryMarkReady()
        {
            if (IsReady)
            {
                return false;
            }

            IsReady = true;
            OnReady();
            _readyCallback?.Invoke(this);
            return true;
        }

        internal void SetReadyCallback(Action<ServerStubEntity>? readyCallback)
        {
            _readyCallback = readyCallback;
        }

        internal StubCallErrorCode ReceiveStubCall(StubCallMessage message)
        {
            if (message.MsgId == 0)
            {
                return StubCallErrorCode.InvalidMessageId;
            }

            return OnStubCall(message);
        }

        protected virtual void OnReady()
        {
            NativeLoggerBridge.Log(ManagedLogLevel.Info, "ServerStubEntity", $"Stub {GetType()} is ready.");
        }

        protected virtual StubCallErrorCode OnStubCall(StubCallMessage message)
        {
            _ = message;
            return StubCallErrorCode.StubRejected;
        }
    }
}

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

        internal StubCallErrorCode ReceiveStubCall(EntityMessage message)
        {
            return ToStubCallErrorCode(ReceiveMailboxCall(message));
        }

        protected virtual void OnReady()
        {
            NativeLoggerBridge.Log(ManagedLogLevel.Info, "ServerStubEntity", $"Stub {GetType()} is ready.");
        }

        protected virtual StubCallErrorCode OnStubCall(EntityMessage message)
        {
            _ = message;
            return StubCallErrorCode.StubRejected;
        }

        protected override MailboxCallErrorCode OnMailboxCall(EntityMessage message)
        {
            return ToMailboxCallErrorCode(OnStubCall(message));
        }

        private static StubCallErrorCode ToStubCallErrorCode(MailboxCallErrorCode result)
        {
            return result switch
            {
                MailboxCallErrorCode.None => StubCallErrorCode.None,
                MailboxCallErrorCode.InvalidArgument => StubCallErrorCode.InvalidArgument,
                MailboxCallErrorCode.InvalidMessageId => StubCallErrorCode.InvalidMessageId,
                MailboxCallErrorCode.UnknownTargetMailbox => StubCallErrorCode.UnknownTargetStub,
                MailboxCallErrorCode.TargetNodeUnavailable => StubCallErrorCode.TargetNodeUnavailable,
                MailboxCallErrorCode.MailboxRejected => StubCallErrorCode.StubRejected,
                _ => StubCallErrorCode.StubRejected,
            };
        }

        private static MailboxCallErrorCode ToMailboxCallErrorCode(StubCallErrorCode result)
        {
            return result switch
            {
                StubCallErrorCode.None => MailboxCallErrorCode.None,
                StubCallErrorCode.InvalidArgument => MailboxCallErrorCode.InvalidArgument,
                StubCallErrorCode.InvalidMessageId => MailboxCallErrorCode.InvalidMessageId,
                StubCallErrorCode.UnknownTargetStub => MailboxCallErrorCode.UnknownTargetMailbox,
                StubCallErrorCode.TargetNodeUnavailable => MailboxCallErrorCode.TargetNodeUnavailable,
                StubCallErrorCode.StubRejected => MailboxCallErrorCode.MailboxRejected,
                _ => MailboxCallErrorCode.MailboxRejected,
            };
        }
    }
}

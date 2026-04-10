using System.Text;
using XServer.Managed.Framework.Entities;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Interop
{
    internal sealed unsafe class ManagedNativeMailboxCallTransport : IMailboxCallTransport
    {
        private readonly ManagedNativeCallbacks _nativeCallbacks;

        private ManagedNativeMailboxCallTransport(ManagedNativeCallbacks nativeCallbacks)
        {
            _nativeCallbacks = nativeCallbacks;
        }

        public static ManagedNativeMailboxCallTransport? CreateOrNull(ManagedNativeCallbacks nativeCallbacks)
        {
            return nativeCallbacks.ForwardStubCall == null
                ? null
                : new ManagedNativeMailboxCallTransport(nativeCallbacks);
        }

        public MailboxCallErrorCode Forward(
            MailboxAddress targetAddress,
            MailboxCallMessage message)
        {
            if (targetAddress == null ||
                targetAddress.EntityId == Guid.Empty ||
                string.IsNullOrWhiteSpace(targetAddress.TargetGameNodeId))
            {
                return MailboxCallErrorCode.InvalidArgument;
            }

            return ForwardCore(
                targetAddress.TargetGameNodeId,
                targetAddress.EntityId.ToString("D"),
                message);
        }

        public MailboxCallErrorCode Forward(
            string stubtype,
            MailboxCallMessage message)
        {
            if (string.IsNullOrWhiteSpace(stubtype))
            {
                return MailboxCallErrorCode.InvalidArgument;
            }

            return ForwardCore(
                string.Empty,
                stubtype,
                message);
        }

        private MailboxCallErrorCode ForwardCore(
            string targetGameNodeId,
            string targetMailboxName,
            MailboxCallMessage message)
        {
            if (string.IsNullOrWhiteSpace(targetMailboxName))
            {
                return MailboxCallErrorCode.InvalidArgument;
            }

            if (message.MsgId == 0)
            {
                return MailboxCallErrorCode.InvalidMessageId;
            }

            if (_nativeCallbacks.ForwardStubCall == null)
            {
                return MailboxCallErrorCode.TargetNodeUnavailable;
            }

            byte[] targetGameNodeIdUtf8 = Encoding.UTF8.GetBytes(targetGameNodeId);
            byte[] targetMailboxNameUtf8 = Encoding.UTF8.GetBytes(targetMailboxName);
            byte[] payloadBytes = message.Payload.ToArray();

            fixed (byte* targetGameNodeIdPtr = targetGameNodeIdUtf8)
            fixed (byte* targetMailboxNamePtr = targetMailboxNameUtf8)
            {
                if (payloadBytes.Length == 0)
                {
                    return ToMailboxCallErrorCode(_nativeCallbacks.ForwardStubCall(
                        _nativeCallbacks.Context,
                        targetGameNodeIdPtr,
                        checked((uint)targetGameNodeIdUtf8.Length),
                        targetMailboxNamePtr,
                        checked((uint)targetMailboxNameUtf8.Length),
                        message.MsgId,
                        null,
                        0));
                }

                fixed (byte* payloadPtr = payloadBytes)
                {
                    return ToMailboxCallErrorCode(_nativeCallbacks.ForwardStubCall(
                        _nativeCallbacks.Context,
                        targetGameNodeIdPtr,
                        checked((uint)targetGameNodeIdUtf8.Length),
                        targetMailboxNamePtr,
                        checked((uint)targetMailboxNameUtf8.Length),
                        message.MsgId,
                        payloadPtr,
                        checked((uint)payloadBytes.Length)));
                }
            }
        }

        private static MailboxCallErrorCode ToMailboxCallErrorCode(int result)
        {
            return result switch
            {
                0 => MailboxCallErrorCode.None,
                (int)MailboxCallErrorCode.InvalidArgument => MailboxCallErrorCode.InvalidArgument,
                (int)MailboxCallErrorCode.InvalidMessageId => MailboxCallErrorCode.InvalidMessageId,
                (int)MailboxCallErrorCode.UnknownTargetMailbox => MailboxCallErrorCode.UnknownTargetMailbox,
                (int)MailboxCallErrorCode.TargetNodeUnavailable => MailboxCallErrorCode.TargetNodeUnavailable,
                (int)MailboxCallErrorCode.MailboxRejected => MailboxCallErrorCode.MailboxRejected,
                _ => MailboxCallErrorCode.InvalidArgument,
            };
        }
    }
}

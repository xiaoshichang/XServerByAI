using System.Text;
using XServer.Managed.Framework.Entities;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Interop
{
    internal sealed unsafe class ManagedNativeStubCallTransport : IStubCallTransport
    {
        private ManagedNativeCallbacks _nativeCallbacks;

        private ManagedNativeStubCallTransport(ManagedNativeCallbacks nativeCallbacks)
        {
            _nativeCallbacks = nativeCallbacks;
        }

        public static ManagedNativeStubCallTransport? CreateOrNull(ManagedNativeCallbacks nativeCallbacks)
        {
            return nativeCallbacks.ForwardStubCall == null
                ? null
                : new ManagedNativeStubCallTransport(nativeCallbacks);
        }

        public StubCallErrorCode Forward(
            string targetStubType,
            string targetGameNodeId,
            StubCallMessage message)
        {
            if (string.IsNullOrWhiteSpace(targetStubType) || string.IsNullOrWhiteSpace(targetGameNodeId))
            {
                return StubCallErrorCode.InvalidArgument;
            }

            if (message.MsgId == 0)
            {
                return StubCallErrorCode.InvalidMessageId;
            }

            if (_nativeCallbacks.ForwardStubCall == null)
            {
                return StubCallErrorCode.TargetNodeUnavailable;
            }

            byte[] targetGameNodeIdUtf8 = Encoding.UTF8.GetBytes(targetGameNodeId);
            byte[] targetStubTypeUtf8 = Encoding.UTF8.GetBytes(targetStubType);
            byte[] payload = message.Payload.ToArray();

            fixed (byte* targetGameNodeIdPtr = targetGameNodeIdUtf8)
            fixed (byte* targetStubTypePtr = targetStubTypeUtf8)
            {
                int result;
                if (payload.Length == 0)
                {
                    result = _nativeCallbacks.ForwardStubCall(
                        _nativeCallbacks.Context,
                        targetGameNodeIdPtr,
                        checked((uint)targetGameNodeIdUtf8.Length),
                        targetStubTypePtr,
                        checked((uint)targetStubTypeUtf8.Length),
                        message.MsgId,
                        null,
                        0);
                }
                else
                {
                    fixed (byte* payloadPtr = payload)
                    {
                        result = _nativeCallbacks.ForwardStubCall(
                            _nativeCallbacks.Context,
                            targetGameNodeIdPtr,
                            checked((uint)targetGameNodeIdUtf8.Length),
                            targetStubTypePtr,
                            checked((uint)targetStubTypeUtf8.Length),
                            message.MsgId,
                            payloadPtr,
                            checked((uint)payload.Length));
                    }
                }

                return ToStubCallErrorCode(result);
            }
        }

        private static StubCallErrorCode ToStubCallErrorCode(int result)
        {
            return result switch
            {
                0 => StubCallErrorCode.None,
                (int)StubCallErrorCode.InvalidArgument => StubCallErrorCode.InvalidArgument,
                (int)StubCallErrorCode.InvalidMessageId => StubCallErrorCode.InvalidMessageId,
                (int)StubCallErrorCode.UnknownTargetStub => StubCallErrorCode.UnknownTargetStub,
                (int)StubCallErrorCode.TargetNodeUnavailable => StubCallErrorCode.TargetNodeUnavailable,
                (int)StubCallErrorCode.StubRejected => StubCallErrorCode.StubRejected,
                _ => StubCallErrorCode.TargetNodeUnavailable,
            };
        }
    }
}

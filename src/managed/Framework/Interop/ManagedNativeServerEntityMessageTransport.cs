using System.Text;
using XServer.Managed.Framework.Entities;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Interop
{
    internal sealed unsafe class ManagedNativeServerEntityMessageTransport : IServerEntityMessageTransport
    {
        private readonly ManagedNativeCallbacks _nativeCallbacks;

        private ManagedNativeServerEntityMessageTransport(ManagedNativeCallbacks nativeCallbacks)
        {
            _nativeCallbacks = nativeCallbacks;
        }

        public static ManagedNativeServerEntityMessageTransport? CreateOrNull(ManagedNativeCallbacks nativeCallbacks)
        {
            return nativeCallbacks.ForwardStubCall == null &&
                   nativeCallbacks.ForwardProxyCall == null &&
                   nativeCallbacks.PushClientMessage == null
                ? null
                : new ManagedNativeServerEntityMessageTransport(nativeCallbacks);
        }

        public MailboxCallErrorCode ForwardByMailbox(
            MailboxAddress targetAddress,
            EntityMessage message)
        {
            if (targetAddress == null ||
                targetAddress.EntityId == Guid.Empty ||
                string.IsNullOrWhiteSpace(targetAddress.TargetGameNodeId))
            {
                return MailboxCallErrorCode.InvalidArgument;
            }

            return ForwardMailboxCore(
                targetAddress.TargetGameNodeId,
                targetAddress.EntityId.ToString("D"),
                message);
        }

        public MailboxCallErrorCode ForwardByStubType(
            string stubType,
            EntityMessage message)
        {
            if (string.IsNullOrWhiteSpace(stubType))
            {
                return MailboxCallErrorCode.InvalidArgument;
            }

            return ForwardMailboxCore(
                string.Empty,
                stubType,
                message);
        }

        public ProxyCallErrorCode ForwardByServerProxy(
            ProxyAddress targetAddress,
            EntityMessage message)
        {
            return ForwardProxyCore(
                targetAddress,
                message,
                _nativeCallbacks.ForwardProxyCall);
        }

        public ProxyCallErrorCode ForwardByClientProxy(
            ProxyAddress targetAddress,
            EntityMessage message)
        {
            return ForwardProxyCore(
                targetAddress,
                message,
                _nativeCallbacks.PushClientMessage);
        }

        private MailboxCallErrorCode ForwardMailboxCore(
            string targetGameNodeId,
            string targetMailboxName,
            EntityMessage message)
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
                int result;
                if (payloadBytes.Length == 0)
                {
                    result = _nativeCallbacks.ForwardStubCall(
                        _nativeCallbacks.Context,
                        targetGameNodeIdPtr,
                        checked((uint)targetGameNodeIdUtf8.Length),
                        targetMailboxNamePtr,
                        checked((uint)targetMailboxNameUtf8.Length),
                        message.MsgId,
                        null,
                        0);
                }
                else
                {
                    fixed (byte* payloadPtr = payloadBytes)
                    {
                        result = _nativeCallbacks.ForwardStubCall(
                            _nativeCallbacks.Context,
                            targetGameNodeIdPtr,
                            checked((uint)targetGameNodeIdUtf8.Length),
                            targetMailboxNamePtr,
                            checked((uint)targetMailboxNameUtf8.Length),
                            message.MsgId,
                            payloadPtr,
                            checked((uint)payloadBytes.Length));
                    }
                }

                return ToMailboxCallErrorCode(result);
            }
        }

        private ProxyCallErrorCode ForwardProxyCore(
            ProxyAddress targetAddress,
            EntityMessage message,
            delegate* unmanaged[Cdecl]<void*, byte*, uint, byte*, uint, uint, byte*, uint, int> forwardCallback)
        {
            if (targetAddress == null ||
                targetAddress.EntityId == Guid.Empty ||
                string.IsNullOrWhiteSpace(targetAddress.RouteGateNodeId))
            {
                return ProxyCallErrorCode.InvalidArgument;
            }

            if (message.MsgId == 0)
            {
                return ProxyCallErrorCode.InvalidMessageId;
            }

            if (forwardCallback == null)
            {
                return ProxyCallErrorCode.TargetNodeUnavailable;
            }

            byte[] routeGateNodeIdUtf8 = Encoding.UTF8.GetBytes(targetAddress.RouteGateNodeId);
            byte[] targetEntityIdUtf8 = Encoding.UTF8.GetBytes(targetAddress.EntityId.ToString("D"));
            byte[] payloadBytes = message.Payload.ToArray();

            fixed (byte* routeGateNodeIdPtr = routeGateNodeIdUtf8)
            fixed (byte* targetEntityIdPtr = targetEntityIdUtf8)
            {
                int result;
                if (payloadBytes.Length == 0)
                {
                    result = forwardCallback(
                        _nativeCallbacks.Context,
                        routeGateNodeIdPtr,
                        checked((uint)routeGateNodeIdUtf8.Length),
                        targetEntityIdPtr,
                        checked((uint)targetEntityIdUtf8.Length),
                        message.MsgId,
                        null,
                        0);
                }
                else
                {
                    fixed (byte* payloadPtr = payloadBytes)
                    {
                        result = forwardCallback(
                            _nativeCallbacks.Context,
                            routeGateNodeIdPtr,
                            checked((uint)routeGateNodeIdUtf8.Length),
                            targetEntityIdPtr,
                            checked((uint)targetEntityIdUtf8.Length),
                            message.MsgId,
                            payloadPtr,
                            checked((uint)payloadBytes.Length));
                    }
                }

                return ToProxyCallErrorCode(result);
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

        private static ProxyCallErrorCode ToProxyCallErrorCode(int result)
        {
            return result switch
            {
                0 => ProxyCallErrorCode.None,
                (int)ProxyCallErrorCode.InvalidArgument => ProxyCallErrorCode.InvalidArgument,
                (int)ProxyCallErrorCode.InvalidMessageId => ProxyCallErrorCode.InvalidMessageId,
                (int)ProxyCallErrorCode.UnknownTargetEntity => ProxyCallErrorCode.UnknownTargetEntity,
                (int)ProxyCallErrorCode.TargetNodeUnavailable => ProxyCallErrorCode.TargetNodeUnavailable,
                (int)ProxyCallErrorCode.EntityRejected => ProxyCallErrorCode.EntityRejected,
                _ => ProxyCallErrorCode.TargetNodeUnavailable,
            };
        }
    }
}

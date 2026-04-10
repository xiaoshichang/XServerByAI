using System.Text;
using XServer.Managed.Framework.Entities;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Interop
{
    internal sealed unsafe class ManagedNativeClientMessageTransport : IProxyCallTransport
    {
        private readonly ManagedNativeCallbacks _nativeCallbacks;

        private ManagedNativeClientMessageTransport(ManagedNativeCallbacks nativeCallbacks)
        {
            _nativeCallbacks = nativeCallbacks;
        }

        public static ManagedNativeClientMessageTransport? CreateOrNull(ManagedNativeCallbacks nativeCallbacks)
        {
            return nativeCallbacks.PushClientMessage == null
                ? null
                : new ManagedNativeClientMessageTransport(nativeCallbacks);
        }

        public ProxyCallErrorCode Forward(
            ProxyAddress targetAddress,
            ProxyCallMessage message)
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

            if (_nativeCallbacks.PushClientMessage == null)
            {
                return ProxyCallErrorCode.TargetNodeUnavailable;
            }

            byte[] routeGateNodeIdUtf8 = Encoding.UTF8.GetBytes(targetAddress.RouteGateNodeId);
            byte[] targetEntityIdUtf8 = Encoding.UTF8.GetBytes(targetAddress.EntityId.ToString("D"));
            byte[] payload = message.Payload.ToArray();

            fixed (byte* routeGateNodeIdPtr = routeGateNodeIdUtf8)
            fixed (byte* targetEntityIdPtr = targetEntityIdUtf8)
            {
                int result;
                if (payload.Length == 0)
                {
                    result = _nativeCallbacks.PushClientMessage(
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
                    fixed (byte* payloadPtr = payload)
                    {
                        result = _nativeCallbacks.PushClientMessage(
                            _nativeCallbacks.Context,
                            routeGateNodeIdPtr,
                            checked((uint)routeGateNodeIdUtf8.Length),
                            targetEntityIdPtr,
                            checked((uint)targetEntityIdUtf8.Length),
                            message.MsgId,
                            payloadPtr,
                            checked((uint)payload.Length));
                    }
                }

                return result switch
                {
                    0 => ProxyCallErrorCode.None,
                    (int)ProxyCallErrorCode.InvalidArgument => ProxyCallErrorCode.InvalidArgument,
                    (int)ProxyCallErrorCode.InvalidMessageId => ProxyCallErrorCode.InvalidMessageId,
                    (int)ProxyCallErrorCode.UnknownTargetEntity => ProxyCallErrorCode.UnknownTargetEntity,
                    (int)ProxyCallErrorCode.TargetNodeUnavailable => ProxyCallErrorCode.TargetNodeUnavailable,
                    _ => ProxyCallErrorCode.TargetNodeUnavailable,
                };
            }
        }
    }
}

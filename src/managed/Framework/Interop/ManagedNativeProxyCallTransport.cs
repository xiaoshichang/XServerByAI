using System.Text;
using XServer.Managed.Framework.Entities;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Interop
{
    internal sealed unsafe class ManagedNativeProxyCallTransport : IProxyCallTransport
    {
        private ManagedNativeCallbacks _nativeCallbacks;

        private ManagedNativeProxyCallTransport(ManagedNativeCallbacks nativeCallbacks)
        {
            _nativeCallbacks = nativeCallbacks;
        }

        public static ManagedNativeProxyCallTransport? CreateOrNull(ManagedNativeCallbacks nativeCallbacks)
        {
            return nativeCallbacks.ForwardProxyCall == null
                ? null
                : new ManagedNativeProxyCallTransport(nativeCallbacks);
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

            if (_nativeCallbacks.ForwardProxyCall == null)
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
                    result = _nativeCallbacks.ForwardProxyCall(
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
                        result = _nativeCallbacks.ForwardProxyCall(
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

                return ToProxyCallErrorCode(result);
            }
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

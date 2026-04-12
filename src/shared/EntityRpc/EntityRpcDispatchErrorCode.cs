#if XSERVER_CLIENT_FRAMEWORK
namespace XServer.Client.Rpc;
#elif XSERVER_MANAGED_FRAMEWORK
namespace XServer.Managed.Framework.Rpc;
#else
#error EntityRpc shared sources must be compiled by a framework project.
#endif

public enum EntityRpcDispatchErrorCode
{
    None = 0,
    InvalidPayload = 1,
    UnsupportedTargetType = 2,
    UnknownRpc = 3,
    InvalidArgumentCount = 4,
    UnsupportedParameterType = 5,
    InvalidArgumentType = 6,
    EntityNotFound = 7,
    TargetInvocationFailed = 8,
    TargetUnavailable = 9,
}

public static class EntityRpcDispatchError
{
    public static string Message(EntityRpcDispatchErrorCode code)
    {
        return code switch
        {
            EntityRpcDispatchErrorCode.None => "No error.",
            EntityRpcDispatchErrorCode.InvalidPayload => "RPC payload is invalid.",
            EntityRpcDispatchErrorCode.UnsupportedTargetType => "RPC target type is not supported.",
            EntityRpcDispatchErrorCode.UnknownRpc => "RPC method could not be found.",
            EntityRpcDispatchErrorCode.InvalidArgumentCount => "RPC argument count does not match the target method.",
            EntityRpcDispatchErrorCode.UnsupportedParameterType => "RPC parameter type is not supported by the current codec.",
            EntityRpcDispatchErrorCode.InvalidArgumentType => "RPC argument type does not match the target parameter.",
            EntityRpcDispatchErrorCode.EntityNotFound => "RPC target entity could not be found.",
            EntityRpcDispatchErrorCode.TargetInvocationFailed => "RPC target invocation failed.",
            EntityRpcDispatchErrorCode.TargetUnavailable => "RPC sender or target route is unavailable.",
            _ => "Unknown RPC dispatch error.",
        };
    }
}

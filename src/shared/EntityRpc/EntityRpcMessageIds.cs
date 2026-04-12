#if XSERVER_CLIENT_FRAMEWORK
namespace XServer.Client.Rpc;
#elif XSERVER_MANAGED_FRAMEWORK
namespace XServer.Managed.Framework.Rpc;
#else
#error EntityRpc shared sources must be compiled by a framework project.
#endif

public static class EntityRpcMessageIds
{
    public const uint ClientToServerEntityRpcMsgId = 6302U;
    public const uint ServerToClientEntityRpcMsgId = 6303U;
}

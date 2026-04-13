#if XSERVER_CLIENT_FRAMEWORK
using XServer.Client.Protocol;

namespace XServer.Client.Rpc;
#elif XSERVER_SERVER_FRAMEWORK
using XServer.Managed.Framework.Protocol;

namespace XServer.Managed.Framework.Rpc;
#else
#error EntityRpc shared sources must be compiled by a framework project.
#endif

public static class EntityRpcMessageIds
{
    public const uint ClientToServerEntityRpcMsgId = ClientServerMessageIds.msgid_client_server_entityrpc;
    public const uint ServerToClientEntityRpcMsgId = ClientServerMessageIds.msgid_server_client_entityrpc;
}

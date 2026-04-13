#if XSERVER_CLIENT_FRAMEWORK
namespace XServer.Client.Protocol;
#elif XSERVER_SERVER_FRAMEWORK
namespace XServer.Managed.Framework.Protocol;
#else
#error Shared protocol sources must be compiled by a framework project.
#endif

public static class ClientServerMessageIds
{
    public const uint msgid_client_server_clienthello = 45010U;
    public const uint msgid_client_server_move = 45011U;
    public const uint msgid_client_server_selectavatar = 45013U;
    public const uint msgid_server_client_broadcast = 6201U;
    public const uint msgid_client_server_entityrpc = 6302U;
    public const uint msgid_server_client_entityrpc = 6303U;

    public const uint ClientHello = msgid_client_server_clienthello;
    public const uint Move = msgid_client_server_move;
    public const uint SelectAvatar = msgid_client_server_selectavatar;
    public const uint Broadcast = msgid_server_client_broadcast;
    public const uint ClientToServerEntityRpc = msgid_client_server_entityrpc;
    public const uint ServerToClientEntityRpc = msgid_server_client_entityrpc;
}

using XServer.Client.Rpc;

namespace XServer.Client.Protocol;

public static class ClientMessageIds
{
    public const uint ClientHello = 45010U;
    public const uint Move = 45011U;
    public const uint SelectAvatar = 45013U;
    public const uint BroadcastMessage = 6201U;
    public const uint ClientToServerEntityRpc = EntityRpcMessageIds.ClientToServerEntityRpcMsgId;
    public const uint ServerToClientEntityRpc = EntityRpcMessageIds.ServerToClientEntityRpcMsgId;
}

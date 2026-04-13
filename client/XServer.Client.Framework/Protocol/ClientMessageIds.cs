namespace XServer.Client.Protocol;

public static class ClientMessageIds
{
    public const uint ClientHello = ClientServerMessageIds.ClientHello;
    public const uint Move = ClientServerMessageIds.Move;
    public const uint SelectAvatar = ClientServerMessageIds.SelectAvatar;
    public const uint BroadcastMessage = ClientServerMessageIds.Broadcast;
    public const uint ClientToServerEntityRpc = ClientServerMessageIds.ClientToServerEntityRpc;
    public const uint ServerToClientEntityRpc = ClientServerMessageIds.ServerToClientEntityRpc;
}

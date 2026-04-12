using XServer.Client.Protocol;

namespace XServer.Client.Rpc;

public sealed record ClientEntityRpcRequest(
    Guid EntityId,
    string RpcName,
    byte[] Payload)
{
    public uint MsgId => ClientMessageIds.ClientToServerEntityRpc;
}

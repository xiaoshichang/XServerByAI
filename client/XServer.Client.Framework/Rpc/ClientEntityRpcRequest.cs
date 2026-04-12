using XServer.Managed.Foundation.Rpc;

namespace XServer.Client.Rpc;

public sealed record ClientEntityRpcRequest(
    Guid EntityId,
    string RpcName,
    byte[] Payload)
{
    public uint MsgId => EntityRpcMessageIds.ClientToServerEntityRpcMsgId;
}

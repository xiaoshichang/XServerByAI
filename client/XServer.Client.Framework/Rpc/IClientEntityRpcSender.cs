using XServer.Client.Entities;

namespace XServer.Client.Rpc;

public interface IClientEntityRpcSender
{
    void SendServerRpc(ClientEntity sourceEntity, ClientEntityRpcRequest request);
}

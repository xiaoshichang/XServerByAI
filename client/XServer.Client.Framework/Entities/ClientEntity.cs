using XServer.Client.Rpc;

namespace XServer.Client.Entities;

public abstract class ClientEntity
{
    private IClientEntityRpcSender? _rpcSender;

    protected ClientEntity(Guid entityId)
    {
        if (entityId == Guid.Empty)
        {
            throw new ArgumentException("Client entityId must not be empty.", nameof(entityId));
        }

        EntityId = entityId;
    }

    public Guid EntityId { get; }

    public string EntityType => GetType().Name;

    public void CallServerRpc(string rpcName, params object?[] arguments)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(rpcName);
        ArgumentNullException.ThrowIfNull(arguments);

        if (_rpcSender is null)
        {
            throw new InvalidOperationException(
                $"Client entity '{EntityType}' does not have an RPC sender configured yet.");
        }

        byte[] payload = EntityRpcJsonCodec.Encode(EntityId, rpcName, arguments);
        _rpcSender.SendServerRpc(this, new ClientEntityRpcRequest(EntityId, rpcName, payload));
    }

    public void CallServerRPC(string rpcName, params object?[] arguments)
    {
        CallServerRpc(rpcName, arguments);
    }

    internal void SetRpcSender(IClientEntityRpcSender? rpcSender)
    {
        _rpcSender = rpcSender;
    }
}

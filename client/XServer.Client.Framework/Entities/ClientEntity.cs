namespace XServer.Client.Entities;

public abstract class ClientEntity
{
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
}

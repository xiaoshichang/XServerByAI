using System.Diagnostics.CodeAnalysis;

namespace XServer.Client.Entities;

public sealed class EntityManager
{
    private readonly Dictionary<Guid, ClientEntity> _entities = [];

    public int Count => _entities.Count;

    public EntityManagerErrorCode Register(ClientEntity entity)
    {
        if (entity == null)
        {
            return EntityManagerErrorCode.InvalidArgument;
        }

        if (_entities.ContainsKey(entity.EntityId))
        {
            return EntityManagerErrorCode.DuplicateEntityId;
        }

        _entities.Add(entity.EntityId, entity);
        return EntityManagerErrorCode.None;
    }

    public EntityManagerErrorCode Unregister(Guid entityId)
    {
        if (!_entities.Remove(entityId))
        {
            return EntityManagerErrorCode.EntityNotFound;
        }

        return EntityManagerErrorCode.None;
    }

    public bool Contains(Guid entityId)
    {
        return _entities.ContainsKey(entityId);
    }

    public bool TryGet(Guid entityId, [NotNullWhen(true)] out ClientEntity? entity)
    {
        return _entities.TryGetValue(entityId, out entity);
    }

    public bool TryGet<T>(Guid entityId, [NotNullWhen(true)] out T? entity)
        where T : ClientEntity
    {
        if (_entities.TryGetValue(entityId, out ClientEntity? resolvedEntity) &&
            resolvedEntity is T typedEntity)
        {
            entity = typedEntity;
            return true;
        }

        entity = null;
        return false;
    }

    public IReadOnlyList<ClientEntity> Snapshot()
    {
        return _entities.Values.ToArray();
    }

    public IReadOnlyList<T> SnapshotByType<T>()
        where T : ClientEntity
    {
        return _entities.Values.OfType<T>().ToArray();
    }

    public void Clear()
    {
        _entities.Clear();
    }
}

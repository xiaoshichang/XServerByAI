using System.Diagnostics.CodeAnalysis;

namespace XServer.Managed.Framework.Entities
{
    public sealed class EntityManager
    {
        private readonly Dictionary<Guid, ServerEntity> _entities = [];

        public int Count => _entities.Count;

        public EntityManagerErrorCode Register(ServerEntity entity)
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

        public bool TryGet(Guid entityId, [NotNullWhen(true)] out ServerEntity? entity)
        {
            return _entities.TryGetValue(entityId, out entity);
        }

        public IReadOnlyList<ServerEntity> Snapshot()
        {
            return _entities.Values.ToArray();
        }

        public IReadOnlyList<T> SnapshotByType<T>()
            where T : ServerEntity
        {
            return _entities.Values.OfType<T>().ToArray();
        }
    }
}

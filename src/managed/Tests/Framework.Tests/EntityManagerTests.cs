using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Tests
{
    public class EntityManagerTests
    {
        [Fact]
        public void EntityManager_RegistersEntities_LooksThemUp_AndSnapshotsByType()
        {
            EntityManager manager = new();
            TrackingEntity entity = new();
            TrackingStubEntity stub = new();

            Assert.Equal(EntityManagerErrorCode.None, manager.Register(entity));
            Assert.Equal(EntityManagerErrorCode.None, manager.Register(stub));

            Assert.Equal(2, manager.Count);
            Assert.True(manager.Contains(entity.EntityId));
            Assert.True(manager.Contains(stub.EntityId));
            Assert.True(manager.TryGet(entity.EntityId, out ServerEntity? resolvedEntity));
            Assert.Same(entity, resolvedEntity);
            Assert.Equal(2, manager.Snapshot().Count);
            Assert.Single(manager.SnapshotByType<TrackingEntity>());
            Assert.Single(manager.SnapshotByType<TrackingStubEntity>());
            Assert.Single(manager.SnapshotByType<ServerStubEntity>());
        }

        [Fact]
        public void EntityManager_RejectsDuplicateEntityIds()
        {
            EntityManager manager = new();
            TrackingEntity first = new();
            TrackingEntity duplicate = new();
            IServerEntityProperties duplicateProperties = duplicate;

            duplicateProperties.EntityId = first.EntityId;

            Assert.Equal(EntityManagerErrorCode.None, manager.Register(first));
            Assert.Equal(EntityManagerErrorCode.DuplicateEntityId, manager.Register(duplicate));
            Assert.Equal(1, manager.Count);
        }

        [Fact]
        public void EntityManager_UnregistersEntities_AndReportsMissingEntries()
        {
            EntityManager manager = new();
            TrackingEntity entity = new();

            Assert.Equal(EntityManagerErrorCode.None, manager.Register(entity));
            Assert.Equal(EntityManagerErrorCode.None, manager.Unregister(entity.EntityId));
            Assert.False(manager.Contains(entity.EntityId));
            Assert.False(manager.TryGet(entity.EntityId, out _));
            Assert.Equal(EntityManagerErrorCode.EntityNotFound, manager.Unregister(entity.EntityId));
            Assert.Equal(0, manager.Count);
        }

        [Fact]
        public void EntityManager_RegistersConcreteGameEntities_AndSnapshotsByConcreteType()
        {
            EntityManager manager = new();
            SpaceEntity space = new();
            AvatarEntity avatar = new();

            Assert.Equal(EntityManagerErrorCode.None, manager.Register(space));
            Assert.Equal(EntityManagerErrorCode.None, manager.Register(avatar));

            Assert.True(manager.TryGet(space.EntityId, out ServerEntity? resolvedSpace));
            Assert.True(manager.TryGet(avatar.EntityId, out ServerEntity? resolvedAvatar));
            Assert.Same(space, resolvedSpace);
            Assert.Same(avatar, resolvedAvatar);
            Assert.Single(manager.SnapshotByType<SpaceEntity>());
            Assert.Single(manager.SnapshotByType<AvatarEntity>());
        }
    }

    internal sealed class TrackingEntity : ServerEntity
    {
    }

    internal sealed class TrackingStubEntity : ServerStubEntity
    {
    }
}

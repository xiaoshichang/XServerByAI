using XServer.Client.Entities;

namespace XServer.Client.Tests;

public sealed class ClientEntityManagerTests
{
    [Fact]
    public void RegisterRejectsDuplicateEntityIds()
    {
        EntityManager manager = new();
        Guid entityId = Guid.NewGuid();
        AvatarEntity first = new(entityId, "demo-account", "Avatar-One");
        AvatarEntity duplicate = new(entityId, "demo-account", "Avatar-Two");

        Assert.Equal(EntityManagerErrorCode.None, manager.Register(first));
        Assert.Equal(EntityManagerErrorCode.DuplicateEntityId, manager.Register(duplicate));
        Assert.True(manager.TryGet<AvatarEntity>(entityId, out AvatarEntity? resolved));
        Assert.Same(first, resolved);
    }

    [Fact]
    public void SnapshotByTypeFiltersRegisteredEntities()
    {
        EntityManager manager = new();
        AvatarEntity avatar = new(Guid.NewGuid(), "demo-account", "Avatar-One");
        TestClientEntity marker = new(Guid.NewGuid());

        Assert.Equal(EntityManagerErrorCode.None, manager.Register(avatar));
        Assert.Equal(EntityManagerErrorCode.None, manager.Register(marker));

        IReadOnlyList<AvatarEntity> avatars = manager.SnapshotByType<AvatarEntity>();

        Assert.Single(avatars);
        Assert.Same(avatar, avatars[0]);

        manager.Clear();

        Assert.Equal(0, manager.Count);
    }

    private sealed class TestClientEntity : ClientEntity
    {
        public TestClientEntity(Guid entityId)
            : base(entityId)
        {
        }
    }
}

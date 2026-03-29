using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Tests
{
    public class ServerEntityTests
    {
        [Fact]
        public void ServerEntity_ExposesExpectedDefaults()
        {
            var entity = new MigratableEntity();

            Assert.Equal(nameof(MigratableEntity), entity.EntityType);
            Assert.True(entity.IsMigratable());
            Assert.Equal(EntityLifecycleState.Constructed, entity.LifecycleState);
        }

        [Fact]
        public void ServerEntity_EntityId_IsMarkedAsEntityProperty()
        {
            EntityPropertyAttribute? entityIdAttribute =
                typeof(ServerEntity).GetProperty(nameof(ServerEntity.EntityId))?
                    .GetCustomAttributes(typeof(EntityPropertyAttribute), inherit: true)
                    .SingleOrDefault() as EntityPropertyAttribute;

            Assert.NotNull(entityIdAttribute);
            Assert.Equal(
                EntityPropertyFlags.AllClients | EntityPropertyFlags.Persistent,
                entityIdAttribute.Flags);
        }

        [Fact]
        public void ServerStubEntity_DefaultsToNonMigratable()
        {
            var entity = new StubEntity();

            Assert.False(entity.IsMigratable());
            Assert.Equal(EntityLifecycleState.Constructed, entity.LifecycleState);
        }

        [Fact]
        public void EntityPropertyAttribute_CanMarkEntityMembers()
        {
            EntityPropertyAttribute? baseAttribute =
                typeof(BasePropertyEntity).GetProperty(nameof(BasePropertyEntity.BaseScore))?
                    .GetCustomAttributes(typeof(EntityPropertyAttribute), inherit: true)
                    .SingleOrDefault() as EntityPropertyAttribute;
            EntityPropertyAttribute? serverOnlyAttribute =
                typeof(DerivedPropertyEntity).GetProperty(nameof(DerivedPropertyEntity.ServerSequence))?
                    .GetCustomAttributes(typeof(EntityPropertyAttribute), inherit: true)
                    .SingleOrDefault() as EntityPropertyAttribute;
            EntityPropertyAttribute? clientServerAttribute =
                typeof(DerivedPropertyEntity).GetProperty(nameof(DerivedPropertyEntity.DisplayName))?
                    .GetCustomAttributes(typeof(EntityPropertyAttribute), inherit: true)
                    .SingleOrDefault() as EntityPropertyAttribute;
            EntityPropertyAttribute? migrationOnlyAttribute =
                typeof(DerivedPropertyEntity).GetProperty(nameof(DerivedPropertyEntity.MigrationOnlySequence))?
                    .GetCustomAttributes(typeof(EntityPropertyAttribute), inherit: true)
                    .SingleOrDefault() as EntityPropertyAttribute;
            EntityPropertyAttribute? persistenceOnlyAttribute =
                typeof(DerivedPropertyEntity).GetProperty(nameof(DerivedPropertyEntity.PersistenceOnlySnapshot))?
                    .GetCustomAttributes(typeof(EntityPropertyAttribute), inherit: true)
                    .SingleOrDefault() as EntityPropertyAttribute;

            Assert.NotNull(baseAttribute);
            Assert.Equal(
                EntityPropertyFlags.AllClients | EntityPropertyFlags.Persistent,
                baseAttribute.Flags);

            Assert.NotNull(serverOnlyAttribute);
            Assert.Equal(EntityPropertyFlags.ServerOnly, serverOnlyAttribute.Flags);

            Assert.NotNull(clientServerAttribute);
            Assert.Equal(EntityPropertyFlags.ClientServer, clientServerAttribute.Flags);

            Assert.NotNull(migrationOnlyAttribute);
            Assert.Equal(EntityPropertyFlags.AllClients, migrationOnlyAttribute.Flags);

            Assert.NotNull(persistenceOnlyAttribute);
            Assert.Equal(EntityPropertyFlags.Persistent, persistenceOnlyAttribute.Flags);
        }

        [Fact]
        public void EntityPropertyAttribute_DefaultsToNoFlags()
        {
            var attribute = new EntityPropertyAttribute();

            Assert.Equal((EntityPropertyFlags)0, attribute.Flags);
        }

        [Fact]
        public void LifecycleStateMachine_AllowsExpectedTransitions_AndInvokesHooksInOrder()
        {
            var entity = new HookTrackingEntity();

            entity.Activate();
            entity.BeginMigration();
            entity.CompleteMigration();
            entity.Deactivate();
            entity.Activate();
            entity.Destroy();

            Assert.Equal(EntityLifecycleState.Destroyed, entity.LifecycleState);
            Assert.Equal(
                ["Activated", "MigrationStarted", "MigrationCompleted", "Deactivated", "Activated", "Destroyed"],
                entity.Hooks);
        }

        [Fact]
        public void LifecycleStateMachine_AllowsMigrationCancellation()
        {
            var entity = new HookTrackingEntity();

            entity.Activate();
            entity.BeginMigration();
            entity.CancelMigration();

            Assert.Equal(EntityLifecycleState.Active, entity.LifecycleState);
            Assert.Equal(["Activated", "MigrationStarted", "MigrationCancelled"], entity.Hooks);
        }

        [Fact]
        public void LifecycleStateMachine_AllowsNonMigratableEntityMigrationForNow()
        {
            var entity = new StubEntity();

            Assert.False(entity.IsMigratable());

            entity.Activate();
            entity.BeginMigration();

            Assert.Equal(EntityLifecycleState.Migrating, entity.LifecycleState);
        }

        [Fact]
        public void LifecycleStateMachine_UpdatesStateWithoutTransitionValidation()
        {
            var entity = new MigratableEntity();

            entity.BeginMigration();
            entity.Destroy();
            entity.Activate();

            Assert.Equal(EntityLifecycleState.Active, entity.LifecycleState);
        }

        private class BasePropertyEntity : ServerEntity
        {
            [EntityProperty(EntityPropertyFlags.AllClients | EntityPropertyFlags.Persistent)]
            public int BaseScore { get; set; }
        }

        private sealed class DerivedPropertyEntity : BasePropertyEntity
        {
            public int RuntimeOnlyCounter { get; set; }

            [EntityProperty(EntityPropertyFlags.ClientServer)]
            public string DisplayName { get; set; } = string.Empty;

            [EntityProperty(EntityPropertyFlags.ServerOnly)]
            public long ServerSequence { get; set; }

            [EntityProperty(EntityPropertyFlags.AllClients)]
            public int MigrationOnlySequence { get; set; }

            [EntityProperty(EntityPropertyFlags.Persistent)]
            public string PersistenceOnlySnapshot { get; set; } = string.Empty;
        }

        private sealed class MigratableEntity : ServerEntity
        {
        }

        private sealed class StubEntity : ServerStubEntity
        {
        }

        private sealed class HookTrackingEntity : ServerEntity
        {
            private readonly List<string> _hooks = [];

            public IReadOnlyList<string> Hooks => _hooks;

            protected override void OnActivated()
            {
                _hooks.Add("Activated");
            }

            protected override void OnMigrationStarted()
            {
                _hooks.Add("MigrationStarted");
            }

            protected override void OnMigrationCompleted()
            {
                _hooks.Add("MigrationCompleted");
            }

            protected override void OnMigrationCancelled()
            {
                _hooks.Add("MigrationCancelled");
            }

            protected override void OnDeactivated()
            {
                _hooks.Add("Deactivated");
            }

            protected override void OnDestroyed()
            {
                _hooks.Add("Destroyed");
            }
        }
    }
}

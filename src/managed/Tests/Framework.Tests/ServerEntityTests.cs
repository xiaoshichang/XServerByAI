using System.Reflection;
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
            Assert.NotEqual(Guid.Empty, entity.EntityId);
            Assert.Equal(EntityLifecycleState.Constructed, entity.LifecycleState);
        }

        [Fact]
        public void ServerEntity_EntityIdField_IsMarkedAsEntityProperty()
        {
            FieldInfo? entityIdField =
                typeof(ServerEntity).GetField("__EntityId", BindingFlags.Instance | BindingFlags.NonPublic);
            EntityPropertyAttribute? entityIdAttribute =
                entityIdField?.GetCustomAttributes(typeof(EntityPropertyAttribute), inherit: false)
                    .SingleOrDefault() as EntityPropertyAttribute;

            Assert.NotNull(entityIdField);
            Assert.True(entityIdField.IsFamily);
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
            Assert.False(entity.IsReady);
            Assert.Equal(EntityLifecycleState.Constructed, entity.LifecycleState);
        }

        [Fact]
        public void ServerStubEntity_TryMarkReady_IsIdempotent_AndInvokesOnReadyOnce()
        {
            var entity = new ReadyTrackingStubEntity();

            Assert.True(entity.TryMarkReady());
            Assert.True(entity.IsReady);
            Assert.False(entity.TryMarkReady());
            Assert.Equal(1, entity.ReadyCallCount);
        }

        [Fact]
        public void EntityPropertySourceGenerator_GeneratesPropertyAccessInterfaces()
        {
            var entity = new DerivedPropertyEntity();

            IServerEntityProperties serverEntityProperties = entity;
            IBasePropertyEntityProperties baseProperties = entity;
            IDerivedPropertyEntityProperties derivedProperties = entity;

            Guid reassignedEntityId = Guid.NewGuid();
            serverEntityProperties.EntityId = reassignedEntityId;
            baseProperties.BaseScore = 42;
            derivedProperties.Label = "label";
            derivedProperties.ServerSequence = 7;
            derivedProperties.MigrationOnlySequence = 11;
            derivedProperties.PersistenceOnlySnapshot = "snapshot";

            Assert.Equal(reassignedEntityId, entity.EntityId);
            Assert.Equal(42, entity.BaseScore);
            Assert.Equal("label", entity.Label);
            Assert.Equal(7L, entity.ServerSequence);
            Assert.Equal(11, entity.MigrationOnlySequence);
            Assert.Equal("snapshot", entity.PersistenceOnlySnapshot);
        }

        [Fact]
        public void EntityPropertyAttribute_CanMarkEntityMembers()
        {
            EntityPropertyAttribute? baseAttribute = GetEntityPropertyAttribute(typeof(BasePropertyEntity), "__BaseScore");
            EntityPropertyAttribute? serverOnlyAttribute =
                GetEntityPropertyAttribute(typeof(DerivedPropertyEntity), "__ServerSequence");
            EntityPropertyAttribute? clientServerAttribute =
                GetEntityPropertyAttribute(typeof(DerivedPropertyEntity), "__Label");
            EntityPropertyAttribute? migrationOnlyAttribute =
                GetEntityPropertyAttribute(typeof(DerivedPropertyEntity), "__MigrationOnlySequence");
            EntityPropertyAttribute? persistenceOnlyAttribute =
                GetEntityPropertyAttribute(typeof(DerivedPropertyEntity), "__PersistenceOnlySnapshot");

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

        private static EntityPropertyAttribute? GetEntityPropertyAttribute(Type declaringType, string fieldName)
        {
            FieldInfo? field =
                declaringType.GetField(fieldName, BindingFlags.Instance | BindingFlags.NonPublic);

            return field?.GetCustomAttributes(typeof(EntityPropertyAttribute), inherit: false)
                .SingleOrDefault() as EntityPropertyAttribute;
        }
    }

    internal partial class BasePropertyEntity : ServerEntity
    {
        [EntityProperty(EntityPropertyFlags.AllClients | EntityPropertyFlags.Persistent)]
        protected int __BaseScore;
    }

    internal partial class DerivedPropertyEntity : BasePropertyEntity
    {
        public int RuntimeOnlyCounter { get; set; }

        [EntityProperty(EntityPropertyFlags.ClientServer)]
        protected string __Label = string.Empty;

        [EntityProperty(EntityPropertyFlags.ServerOnly)]
        protected long __ServerSequence;

        [EntityProperty(EntityPropertyFlags.AllClients)]
        protected int __MigrationOnlySequence;

        [EntityProperty(EntityPropertyFlags.Persistent)]
        protected string __PersistenceOnlySnapshot = string.Empty;
    }

    internal sealed class MigratableEntity : ServerEntity
    {
    }

    internal sealed class StubEntity : ServerStubEntity
    {
    }

    internal sealed class ReadyTrackingStubEntity : ServerStubEntity
    {
        public int ReadyCallCount { get; private set; }

        protected override void OnReady()
        {
            ReadyCallCount++;
        }
    }

    internal sealed class HookTrackingEntity : ServerEntity
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

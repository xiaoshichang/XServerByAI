using System;

namespace XServer.Managed.Framework.Entities
{
    public abstract partial class ServerEntity
    {
        protected ServerEntity(MobilityType mobilityType = MobilityType.Migratable)
        {
            EntityId = Guid.NewGuid();
            MobilityType = mobilityType;
            LifecycleState = EntityLifecycleState.Constructed;
        }

        [EntityProperty(EntityPropertyFlags.AllClients | EntityPropertyFlags.Persistent)]
        public Guid EntityId { get; }

        public string EntityType => GetType().Name;

        public MobilityType MobilityType { get; }
    }
}

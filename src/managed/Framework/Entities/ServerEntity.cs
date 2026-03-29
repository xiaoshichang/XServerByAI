using System;

namespace XServer.Managed.Framework.Entities
{
    public abstract partial class ServerEntity
    {
        protected ServerEntity()
        {
            EntityId = Guid.NewGuid();
            LifecycleState = EntityLifecycleState.Constructed;
        }

        [EntityProperty(EntityPropertyFlags.AllClients | EntityPropertyFlags.Persistent)]
        protected Guid __EntityId;

        public string EntityType => GetType().Name;

        public virtual bool IsMigratable()
        {
            return true;
        }
    }
}

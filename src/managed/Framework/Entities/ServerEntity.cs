using System;

namespace XServer.Managed.Framework.Entities
{
    // Entity identity, lifecycle hooks, and state management are introduced by later milestones.
    public abstract class ServerEntity
    {
        protected ServerEntity()
        {
            EntityId = Guid.NewGuid();
        }

        public Guid EntityId { get; }
    }
}

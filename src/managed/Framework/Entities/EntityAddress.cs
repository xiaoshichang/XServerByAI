namespace XServer.Managed.Framework.Entities
{
    // All entity addresses try local dispatch first; remote dispatch is always mediated by Gate.
    public abstract class EntityAddress
    {
        protected EntityAddress(Guid entityId)
        {
            EntityId = entityId;
        }

        public Guid EntityId { get; }

        public bool ShouldAttemptLocalDispatchFirst => true;
    }
}
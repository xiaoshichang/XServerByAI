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

    // When local lookup misses, Gate forwards mailbox traffic directly to the declared target game node.
    public sealed class MailboxAddress : EntityAddress
    {
        public MailboxAddress(Guid entityId, string targetGameNodeId)
            : base(entityId)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(targetGameNodeId);
            TargetGameNodeId = targetGameNodeId;
        }

        public string TargetGameNodeId { get; }
    }

    // When local lookup misses, Gate uses the designated routing table to resolve the current owner.
    public sealed class ProxyAddress : EntityAddress
    {
        public ProxyAddress(Guid entityId, string routeGateNodeId)
            : base(entityId)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(routeGateNodeId);
            RouteGateNodeId = routeGateNodeId;
        }

        public string RouteGateNodeId { get; }
    }
}

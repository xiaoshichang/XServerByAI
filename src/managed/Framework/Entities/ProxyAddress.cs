namespace XServer.Managed.Framework.Entities
{
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
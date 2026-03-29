namespace XServer.Managed.GameLogic.Runtime
{
    public sealed class ServerStubOwnershipAssignment
    {
        public ServerStubOwnershipAssignment(string entityType, string entityId, string ownerGameNodeId)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(entityType);
            ArgumentException.ThrowIfNullOrWhiteSpace(entityId);
            ArgumentException.ThrowIfNullOrWhiteSpace(ownerGameNodeId);

            EntityType = entityType;
            EntityId = entityId;
            OwnerGameNodeId = ownerGameNodeId;
        }

        public string EntityType { get; }

        public string EntityId { get; }

        public string OwnerGameNodeId { get; }
    }
}

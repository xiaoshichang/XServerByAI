namespace XServer.Managed.Framework.Entities
{
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
}
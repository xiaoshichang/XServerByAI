using System;

namespace XServer.Managed.Framework.Entities
{
    public sealed class AvatarEntity : ServerEntity
    {
        public string AccountId { get; private set; } = string.Empty;

        public string DisplayName { get; private set; } = string.Empty;

        public ProxyAddress? Proxy { get; private set; }

        public override bool IsMigratable()
        {
            return true;
        }

        public void BindIdentity(
            Guid entityId,
            string accountId,
            string displayName,
            string routeGateNodeId)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(accountId);
            ArgumentException.ThrowIfNullOrWhiteSpace(routeGateNodeId);
            if (entityId == Guid.Empty)
            {
                throw new ArgumentException("Avatar entityId must not be empty.", nameof(entityId));
            }

            IServerEntityProperties properties = this;
            properties.EntityId = entityId;
            AccountId = accountId;
            DisplayName = string.IsNullOrWhiteSpace(displayName) ? entityId.ToString("D") : displayName;
            Proxy = new ProxyAddress(EntityId, routeGateNodeId);
        }
    }
}

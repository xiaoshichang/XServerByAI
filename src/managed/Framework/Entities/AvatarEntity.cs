using System;
using XServer.Managed.Framework.Interop;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Entities
{
    public sealed class AvatarEntity : ServerEntity
    {
        private readonly List<ReceivedProxyMessage> _receivedProxyMessages = [];

        public string AccountId { get; private set; } = string.Empty;

        public string DisplayName { get; private set; } = string.Empty;

        public ProxyAddress? Proxy { get; private set; }

        public IReadOnlyList<ReceivedProxyMessage> ReceivedProxyMessages => _receivedProxyMessages;

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
            if (entityId == Guid.Empty)
            {
                throw new ArgumentException("Avatar entityId must not be empty.", nameof(entityId));
            }

            IServerEntityProperties properties = this;
            properties.EntityId = entityId;
            AccountId = accountId;
            DisplayName = string.IsNullOrWhiteSpace(displayName) ? entityId.ToString("D") : displayName;
            RebindProxy(routeGateNodeId);
        }

        public void RebindProxy(string routeGateNodeId)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(routeGateNodeId);
            if (EntityId == Guid.Empty)
            {
                throw new InvalidOperationException("Avatar entityId must be assigned before binding a proxy.");
            }

            Proxy = new ProxyAddress(EntityId, routeGateNodeId);
        }

        protected override ProxyCallErrorCode OnProxyCall(ProxyCallMessage message)
        {
            NativeLoggerBridge.Info(nameof(AvatarEntity), $"AvatarEntity {EntityId:D} received proxy call msgId={message.MsgId}.");
            _receivedProxyMessages.Add(new ReceivedProxyMessage(message.MsgId, message.Payload.ToArray()));
            if (Proxy != null)
            {
                PushToClient(Proxy, message.MsgId, message.Payload);
            }
            return ProxyCallErrorCode.None;
        }

        public readonly record struct ReceivedProxyMessage(uint MsgId, byte[] Payload);
    }
}

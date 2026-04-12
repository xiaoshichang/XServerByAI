using System;
using XServer.Managed.Framework.Interop;
using XServer.Managed.Framework.Rpc;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Entities
{
    public sealed class AvatarEntity : ServerEntity
    {
        private readonly List<ReceivedProxyMessage> _receivedProxyMessages = [];

        public string AccountId { get; private set; } = string.Empty;

        public ProxyAddress? Proxy { get; private set; }

        public IReadOnlyList<ReceivedProxyMessage> ReceivedProxyMessages => _receivedProxyMessages;

        public string Weapon { get; private set; } = string.Empty;

        public override bool IsMigratable()
        {
            return true;
        }

        public void BindIdentity(
            Guid entityId,
            string accountId,
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

        [ServerRPC]
        public void SetWeapon(string weapon)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(weapon);
            Weapon = weapon;
            NativeLoggerBridge.Info(
                nameof(AvatarEntity),
                $"AvatarEntity {EntityId:D} updated Weapon accountId={AccountId} weapon={Weapon}.");
            CallClientRpc("OnSetWeaponResult", Weapon, true);
        }

        protected override ProxyCallErrorCode OnProxyCall(EntityMessage message)
        {
            NativeLoggerBridge.Info(nameof(AvatarEntity), $"AvatarEntity {EntityId:D} received proxy call msgId={message.MsgId}.");
            _receivedProxyMessages.Add(new ReceivedProxyMessage(message.MsgId, message.Payload.ToArray()));
            if (Proxy != null)
            {
                PushToClient(Proxy, message.MsgId, message.Payload);
            }

            return ProxyCallErrorCode.None;
        }

        protected override ProxyAddress? GetDefaultClientRpcTargetAddress()
        {
            return Proxy;
        }

        public readonly record struct ReceivedProxyMessage(uint MsgId, byte[] Payload);
    }
}

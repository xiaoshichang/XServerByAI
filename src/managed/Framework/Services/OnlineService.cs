using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json;
using XServer.Managed.Framework.Interop;
using XServer.Managed.Framework.Protocol;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Entities
{
    public readonly record struct OnlineAvatarRegistration(
        string AccountId,
        Guid EntityId,
        ulong SessionId,
        string GateNodeId,
        string GameNodeId,
        ProxyAddress Proxy);

    internal static class OnlineAvatarRegistrationPayloadCodec
    {
        private static readonly JsonSerializerOptions JsonOptions = new()
        {
            PropertyNameCaseInsensitive = true,
        };

        public static byte[] Encode(OnlineAvatarRegistration registration)
        {
            if (string.IsNullOrWhiteSpace(registration.AccountId) ||
                registration.EntityId == Guid.Empty ||
                registration.SessionId == 0 ||
                string.IsNullOrWhiteSpace(registration.GateNodeId) ||
                string.IsNullOrWhiteSpace(registration.GameNodeId) ||
                registration.Proxy == null ||
                registration.Proxy.EntityId != registration.EntityId ||
                string.IsNullOrWhiteSpace(registration.Proxy.RouteGateNodeId))
            {
                throw new ArgumentException("Online avatar registration is incomplete.", nameof(registration));
            }

            OnlineAvatarRegistrationPayload payload = new()
            {
                AccountId = registration.AccountId,
                EntityId = registration.EntityId.ToString("D"),
                SessionId = registration.SessionId,
                GateNodeId = registration.GateNodeId,
                GameNodeId = registration.GameNodeId,
                ProxyEntityId = registration.Proxy.EntityId.ToString("D"),
                ProxyRouteGateNodeId = registration.Proxy.RouteGateNodeId,
            };
            return JsonSerializer.SerializeToUtf8Bytes(payload, JsonOptions);
        }

        public static bool TryDecode(ReadOnlyMemory<byte> payload, out OnlineAvatarRegistration registration)
        {
            registration = default;
            if (payload.IsEmpty)
            {
                return false;
            }

            OnlineAvatarRegistrationPayload? decoded = JsonSerializer.Deserialize<OnlineAvatarRegistrationPayload>(payload.Span, JsonOptions);
            if (decoded == null ||
                string.IsNullOrWhiteSpace(decoded.AccountId) ||
                string.IsNullOrWhiteSpace(decoded.EntityId) ||
                decoded.SessionId == 0 ||
                string.IsNullOrWhiteSpace(decoded.GateNodeId) ||
                string.IsNullOrWhiteSpace(decoded.GameNodeId) ||
                string.IsNullOrWhiteSpace(decoded.ProxyEntityId) ||
                string.IsNullOrWhiteSpace(decoded.ProxyRouteGateNodeId) ||
                !Guid.TryParse(decoded.EntityId, out Guid entityId) ||
                entityId == Guid.Empty ||
                !Guid.TryParse(decoded.ProxyEntityId, out Guid proxyEntityId) ||
                proxyEntityId != entityId)
            {
                return false;
            }

            registration = new OnlineAvatarRegistration(
                decoded.AccountId,
                entityId,
                decoded.SessionId,
                decoded.GateNodeId,
                decoded.GameNodeId,
                new ProxyAddress(proxyEntityId, decoded.ProxyRouteGateNodeId));
            return true;
        }

        private sealed class OnlineAvatarRegistrationPayload
        {
            public string? AccountId { get; init; }

            public string? EntityId { get; init; }

            public ulong SessionId { get; init; }

            public string? GateNodeId { get; init; }

            public string? GameNodeId { get; init; }

            public string? ProxyEntityId { get; init; }

            public string? ProxyRouteGateNodeId { get; init; }
        }
    }

    public sealed class OnlineStub : ServerStubEntity
    {
        private const string StartupTargetStubType = "MatchStub";
        private const uint StartupCallMsgId = 5101u;
        public const uint RegisterOnlineAvatarMessageStubMsgId = 5200u;
        public const uint BroadcastOnlineAvatarMessageStubMsgId = 5201u;
        public const uint BroadcastOnlineAvatarProxyMsgId = ClientServerMessageIds.msgid_server_client_broadcast;
        private readonly Dictionary<Guid, OnlineAvatarRegistration> _registeredAvatars = [];
        private long _startupTimerId;

        public bool TryGetRegisteredAvatar(Guid entityId, out OnlineAvatarRegistration registration)
        {
            if (entityId == Guid.Empty)
            {
                throw new ArgumentException("Avatar entityId must not be empty.", nameof(entityId));
            }

            return _registeredAvatars.TryGetValue(entityId, out registration);
        }

        public IReadOnlyList<OnlineAvatarRegistration> SnapshotRegisteredAvatars()
        {
            return _registeredAvatars.Values
                .OrderBy(static entry => entry.EntityId)
                .ToArray();
        }

        protected override void OnReady()
        {
            base.OnReady();

            if (_startupTimerId > 0)
            {
                return;
            }

            long timerId = CreateNativeOnceTimer(TimeSpan.FromSeconds(5), HandleStartupTimerFired);
            if (NativeTimerResult.IsTimerId(timerId))
            {
                _startupTimerId = timerId;
            }
        }

        protected override void OnDestroyed()
        {
            if (_startupTimerId > 0)
            {
                _ = CancelNativeTimer(_startupTimerId);
                _startupTimerId = 0;
            }
        }

        protected override StubCallErrorCode OnStubCall(EntityMessage message)
        {
            if (message.MsgId == RegisterOnlineAvatarMessageStubMsgId)
            {
                return HandleRegisterOnlineAvatar(message);
            }

            if (message.MsgId == BroadcastOnlineAvatarMessageStubMsgId)
            {
                return HandleBroadcastOnlineAvatar(message);
            }

            return base.OnStubCall(message);
        }

        private StubCallErrorCode HandleRegisterOnlineAvatar(EntityMessage message)
        {
            if (!OnlineAvatarRegistrationPayloadCodec.TryDecode(message.Payload, out OnlineAvatarRegistration registration))
            {
                NativeLoggerBridge.Warn(nameof(OnlineStub), "OnlineStub rejected an invalid avatar registration payload.");
                return StubCallErrorCode.InvalidArgument;
            }

            if (_registeredAvatars.TryGetValue(registration.EntityId, out OnlineAvatarRegistration existing) &&
                !string.Equals(existing.AccountId, registration.AccountId, StringComparison.Ordinal))
            {
                NativeLoggerBridge.Warn(
                    nameof(OnlineStub),
                    $"OnlineStub rejected avatar registration because entityId={registration.EntityId:D} is already bound to another account.");
                return StubCallErrorCode.StubRejected;
            }

            _registeredAvatars[registration.EntityId] = registration;
            NativeLoggerBridge.Info(
                nameof(OnlineStub),
                $"OnlineStub registered avatar entityId={registration.EntityId:D} routeGateNodeId={registration.Proxy.RouteGateNodeId} gameNodeId={registration.GameNodeId}.");
            return StubCallErrorCode.None;
        }

        private StubCallErrorCode HandleBroadcastOnlineAvatar(EntityMessage message)
        {
            IReadOnlyList<OnlineAvatarRegistration> registrations = SnapshotRegisteredAvatars();
            foreach (OnlineAvatarRegistration registration in registrations)
            {
                CallProxy(registration.Proxy, BroadcastOnlineAvatarProxyMsgId, message.Payload);
            }

            NativeLoggerBridge.Info(
                nameof(OnlineStub),
                $"OnlineStub broadcasted msgId={BroadcastOnlineAvatarProxyMsgId} to {registrations.Count} online avatars.");
            return StubCallErrorCode.None;
        }

        private void HandleStartupTimerFired()
        {
            _startupTimerId = 0;

            CallStub(
                StartupTargetStubType,
                StartupCallMsgId,
                Encoding.UTF8.GetBytes("online-startup-call"));
        }
    }
}

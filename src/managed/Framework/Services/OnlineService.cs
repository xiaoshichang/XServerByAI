using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using XServer.Managed.Framework.Interop;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Entities
{
    public readonly record struct OnlineAvatarRegistration(
        string AccountId,
        Guid EntityId,
        ulong SessionId,
        string GateNodeId,
        string GameNodeId,
        string DisplayName);

    public sealed class OnlineStub : ServerStubEntity
    {
        private const string StartupTargetStubType = "MatchStub";
        private const uint StartupCallMsgId = 5101u;
        private static readonly object AvatarRegistrySync = new();
        private static readonly Dictionary<Guid, OnlineAvatarRegistration> RegisteredAvatars = [];
        private long _startupTimerId;

        public static bool TryRegisterAvatar(OnlineAvatarRegistration registration, out string error)
        {
            if (string.IsNullOrWhiteSpace(registration.AccountId) ||
                registration.EntityId == Guid.Empty ||
                registration.SessionId == 0 ||
                string.IsNullOrWhiteSpace(registration.GateNodeId) ||
                string.IsNullOrWhiteSpace(registration.GameNodeId))
            {
                error = "Online avatar registration is incomplete.";
                return false;
            }

            lock (AvatarRegistrySync)
            {
                if (RegisteredAvatars.TryGetValue(registration.EntityId, out OnlineAvatarRegistration existing))
                {
                    bool sameRegistration =
                        existing.EntityId == registration.EntityId &&
                        existing.SessionId == registration.SessionId &&
                        string.Equals(existing.AccountId, registration.AccountId, StringComparison.Ordinal) &&
                        string.Equals(existing.GateNodeId, registration.GateNodeId, StringComparison.Ordinal) &&
                        string.Equals(existing.GameNodeId, registration.GameNodeId, StringComparison.Ordinal);
                    if (!sameRegistration)
                    {
                        error = $"Avatar entity '{registration.EntityId:D}' is already registered online.";
                        return false;
                    }
                }

                RegisteredAvatars[registration.EntityId] = registration;
            }

            error = string.Empty;
            return true;
        }

        public static bool TryGetRegisteredAvatar(Guid entityId, out OnlineAvatarRegistration registration)
        {
            if (entityId == Guid.Empty)
            {
                throw new ArgumentException("Avatar entityId must not be empty.", nameof(entityId));
            }

            lock (AvatarRegistrySync)
            {
                return RegisteredAvatars.TryGetValue(entityId, out registration);
            }
        }

        public static IReadOnlyList<OnlineAvatarRegistration> SnapshotRegisteredAvatars()
        {
            lock (AvatarRegistrySync)
            {
                return RegisteredAvatars.Values
                    .OrderBy(static entry => entry.EntityId)
                    .ToArray();
            }
        }

        public static void ClearRegisteredAvatars()
        {
            lock (AvatarRegistrySync)
            {
                RegisteredAvatars.Clear();
            }
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

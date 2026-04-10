using XServer.Managed.Framework.Entities;
using XServer.Managed.Framework.Runtime;
using XServer.Managed.GameLogic.Services;

namespace XServer.Managed.Framework.Tests
{
    public class GameNodeRuntimeStateTests
    {
        [Fact]
        public void GameNodeRuntimeState_AppliesLocalOwnership_RegistersOwnedStubs_AndMarksThemReady()
        {
            List<(ulong AssignmentEpoch, string EntityType, Guid EntityId, bool IsReady)> readyNotifications = [];
            GameNodeRuntimeState runtimeState = new("Game0", (assignmentEpoch, stub) =>
            {
                readyNotifications.Add((assignmentEpoch, stub.EntityType, stub.EntityId, stub.IsReady));
            });

            ServerStubOwnershipSnapshot snapshot = new(
                7,
                [
                    new ServerStubOwnershipAssignment("MatchStub", "unknown", "Game0"),
                    new ServerStubOwnershipAssignment("ChatStub", "unknown", "Game9"),
                    new ServerStubOwnershipAssignment("LeaderboardStub", "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(snapshot));

            Assert.Equal((ulong)7, runtimeState.AssignmentEpoch);
            Assert.True(runtimeState.HasOwnedServerStubs);
            Assert.True(runtimeState.IsLocalReady);
            Assert.Equal(2, runtimeState.EntityManager.Count);
            Assert.Collection(
                runtimeState.OwnedServerStubs,
                stub =>
                {
                    Assert.IsType<MatchStub>(stub);
                    Assert.True(stub.IsReady);
                    Assert.NotEqual(Guid.Empty, stub.EntityId);
                },
                stub =>
                {
                    Assert.IsType<LeaderboardStub>(stub);
                    Assert.True(stub.IsReady);
                    Assert.NotEqual(Guid.Empty, stub.EntityId);
                });
            Assert.Collection(
                runtimeState.ReadyServerStubs,
                stub => Assert.Equal("MatchStub", stub.EntityType),
                stub => Assert.Equal("LeaderboardStub", stub.EntityType));
            Assert.Collection(
                readyNotifications,
                notification =>
                {
                    Assert.Equal((ulong)7, notification.AssignmentEpoch);
                    Assert.Equal("MatchStub", notification.EntityType);
                    Assert.NotEqual(Guid.Empty, notification.EntityId);
                    Assert.True(notification.IsReady);
                },
                notification =>
                {
                    Assert.Equal((ulong)7, notification.AssignmentEpoch);
                    Assert.Equal("LeaderboardStub", notification.EntityType);
                    Assert.NotEqual(Guid.Empty, notification.EntityId);
                    Assert.True(notification.IsReady);
                });
            Assert.Single(runtimeState.EntityManager.SnapshotByType<MatchStub>());
            Assert.Single(runtimeState.EntityManager.SnapshotByType<LeaderboardStub>());
            Assert.Empty(runtimeState.EntityManager.SnapshotByType<ChatStub>());
        }

        [Fact]
        public void GameNodeRuntimeState_ReplayingSameLocalOwnership_IsIdempotent()
        {
            List<string> readyNotifications = [];
            GameNodeRuntimeState runtimeState = new("Game0", (_, stub) => readyNotifications.Add(stub.EntityType));

            ServerStubOwnershipSnapshot firstSnapshot = new(
                11,
                [
                    new ServerStubOwnershipAssignment("MatchStub", "unknown", "Game0"),
                    new ServerStubOwnershipAssignment("LeaderboardStub", "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(firstSnapshot));

            ServerStubEntity firstMatch = runtimeState.OwnedServerStubs[0];
            ServerStubEntity firstLeaderboard = runtimeState.OwnedServerStubs[1];
            Guid firstMatchId = firstMatch.EntityId;
            Guid firstLeaderboardId = firstLeaderboard.EntityId;

            ServerStubOwnershipSnapshot replaySnapshot = new(
                11,
                [
                    new ServerStubOwnershipAssignment("MatchStub", firstMatchId.ToString(), "Game0"),
                    new ServerStubOwnershipAssignment("LeaderboardStub", firstLeaderboardId.ToString(), "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(replaySnapshot));

            Assert.Same(firstMatch, runtimeState.OwnedServerStubs[0]);
            Assert.Same(firstLeaderboard, runtimeState.OwnedServerStubs[1]);
            Assert.Equal(firstMatchId, runtimeState.OwnedServerStubs[0].EntityId);
            Assert.Equal(firstLeaderboardId, runtimeState.OwnedServerStubs[1].EntityId);
            Assert.Equal(2, runtimeState.EntityManager.Count);
            Assert.Equal(["MatchStub", "LeaderboardStub"], readyNotifications);
        }

        [Fact]
        public void GameNodeRuntimeState_ChangingOwnership_RemovesRevokedStubs_AndPreservesOtherEntities()
        {
            GameNodeRuntimeState runtimeState = new("Game0");
            IndependentEntity unrelatedEntity = new();

            Assert.Equal(EntityManagerErrorCode.None, runtimeState.EntityManager.Register(unrelatedEntity));

            ServerStubOwnershipSnapshot firstSnapshot = new(
                1,
                [
                    new ServerStubOwnershipAssignment("MatchStub", "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(firstSnapshot));

            ServerStubEntity revokedStub = runtimeState.OwnedServerStubs[0];

            ServerStubOwnershipSnapshot secondSnapshot = new(
                2,
                [
                    new ServerStubOwnershipAssignment("ChatStub", "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(secondSnapshot));

            Assert.True(runtimeState.EntityManager.Contains(unrelatedEntity.EntityId));
            Assert.False(runtimeState.EntityManager.Contains(revokedStub.EntityId));
            Assert.Equal(EntityLifecycleState.Destroyed, revokedStub.LifecycleState);
            Assert.Single(runtimeState.OwnedServerStubs);
            Assert.IsType<ChatStub>(runtimeState.OwnedServerStubs[0]);
            Assert.True(runtimeState.OwnedServerStubs[0].IsReady);
            Assert.Equal(2, runtimeState.EntityManager.Count);
        }

        [Fact]
        public void GameNodeRuntimeState_RejectsUnknownStubType_WithoutMutatingCurrentState()
        {
            GameNodeRuntimeState runtimeState = new("Game0");

            ServerStubOwnershipSnapshot validSnapshot = new(
                3,
                [
                    new ServerStubOwnershipAssignment("MatchStub", "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(validSnapshot));

            ServerStubEntity currentStub = runtimeState.OwnedServerStubs[0];
            Guid currentEntityId = currentStub.EntityId;

            ServerStubOwnershipSnapshot invalidSnapshot = new(
                4,
                [
                    new ServerStubOwnershipAssignment("MissingService", "unknown", "Game0"),
                ]);

            Assert.Equal(
                GameNodeRuntimeStateErrorCode.UnknownStubType,
                runtimeState.ApplyOwnership(invalidSnapshot));
            Assert.Equal((ulong)3, runtimeState.AssignmentEpoch);
            Assert.Same(currentStub, runtimeState.OwnedServerStubs[0]);
            Assert.Equal(currentEntityId, runtimeState.OwnedServerStubs[0].EntityId);
            Assert.True(runtimeState.EntityManager.Contains(currentEntityId));
            Assert.Equal(1, runtimeState.EntityManager.Count);
        }

        [Fact]
        public void GameNodeRuntimeState_CreatesAvatarEntity_RegistersProxyThroughOwnedOnlineStub()
        {
            LoopbackMailboxCallTransport transport = new();
            GameNodeRuntimeState onlineRuntime = new("Game0", mailboxCallTransport: transport);
            GameNodeRuntimeState avatarRuntime = new("Game3", mailboxCallTransport: transport);
            transport.Register(onlineRuntime);
            transport.Register(avatarRuntime);

            ServerStubOwnershipSnapshot snapshot = new(
                1,
                [
                    new ServerStubOwnershipAssignment(nameof(OnlineStub), "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, onlineRuntime.ApplyOwnership(snapshot));
            Assert.Equal(GameNodeRuntimeStateErrorCode.None, avatarRuntime.ApplyOwnership(snapshot));

            Guid avatarEntityId = Guid.NewGuid();
            AvatarEntitySpawnRequest request = new(
                avatarEntityId,
                "account-alpha",
                "Hero Alpha",
                "Gate7",
                42);

            Assert.True(avatarRuntime.TryCreateAvatarEntity(request, out AvatarEntity? avatar, out string? error));
            Assert.Null(error);
            Assert.NotNull(avatar);
            Assert.Equal(EntityLifecycleState.Active, avatar!.LifecycleState);
            Assert.Equal(avatarEntityId, avatar.EntityId);
            Assert.Equal("account-alpha", avatar.AccountId);
            Assert.Equal("Hero Alpha", avatar.DisplayName);
            Assert.NotNull(avatar.Proxy);
            Assert.Equal("Gate7", avatar.Proxy!.RouteGateNodeId);
            Assert.True(avatarRuntime.EntityManager.Contains(avatar.EntityId));
            Assert.Single(avatarRuntime.EntityManager.SnapshotByType<AvatarEntity>());

            OnlineStub onlineStub = Assert.IsType<OnlineStub>(onlineRuntime.OwnedServerStubs.Single());
            Assert.True(onlineStub.TryGetRegisteredAvatar(avatarEntityId, out OnlineAvatarRegistration registration));
            Assert.Equal(avatar.EntityId, registration.EntityId);
            Assert.Equal((ulong)42, registration.SessionId);
            Assert.Equal("Gate7", registration.GateNodeId);
            Assert.Equal("Game3", registration.GameNodeId);
            Assert.Equal(avatar.EntityId, registration.Proxy.EntityId);
            Assert.Equal("Gate7", registration.Proxy.RouteGateNodeId);
            Assert.Equal(1, transport.ForwardCallCount);
            Assert.Equal(nameof(OnlineStub), transport.LastTargetMailboxName);
            Assert.Equal(string.Empty, transport.LastTargetGameNodeId);
        }

        [Fact]
        public void GameNodeRuntimeState_ReceiveMailboxCall_DeliversToNonMigratableEntity()
        {
            GameNodeRuntimeState runtimeState = new("Game0");
            NonMigratableMailboxEntity entity = new();

            Assert.Equal(EntityManagerErrorCode.None, runtimeState.EntityManager.Register(entity));

            byte[] payload = [0x41, 0x42, 0x43];
            MailboxCallErrorCode result = runtimeState.ReceiveMailboxCall(
                entity.EntityId.ToString("D"),
                new MailboxCallMessage(7101u, payload));

            Assert.Equal(MailboxCallErrorCode.None, result);
            Assert.Collection(
                entity.ReceivedMailboxCalls,
                received =>
                {
                    Assert.Equal(7101u, received.MsgId);
                    Assert.Equal(payload, received.Payload);
                });
        }

        [Fact]
        public void GameNodeRuntimeState_ReceiveMailboxCall_RejectsMigratableEntity()
        {
            GameNodeRuntimeState runtimeState = new("Game0");
            IndependentEntity entity = new();

            Assert.Equal(EntityManagerErrorCode.None, runtimeState.EntityManager.Register(entity));

            MailboxCallErrorCode result = runtimeState.ReceiveMailboxCall(
                entity.EntityId.ToString("D"),
                new MailboxCallMessage(7102u, Array.Empty<byte>()));

            Assert.Equal(MailboxCallErrorCode.MailboxRejected, result);
        }
    }

    internal sealed class IndependentEntity : ServerEntity
    {
    }

    internal sealed class NonMigratableMailboxEntity : ServerEntity
    {
        private readonly List<ReceivedMailboxCall> _receivedMailboxCalls = [];

        public IReadOnlyList<ReceivedMailboxCall> ReceivedMailboxCalls => _receivedMailboxCalls;

        public override bool IsMigratable()
        {
            return false;
        }

        protected override MailboxCallErrorCode OnMailboxCall(MailboxCallMessage message)
        {
            _receivedMailboxCalls.Add(new ReceivedMailboxCall(message.MsgId, message.Payload.ToArray()));
            return MailboxCallErrorCode.None;
        }

        internal readonly record struct ReceivedMailboxCall(uint MsgId, byte[] Payload);
    }
}

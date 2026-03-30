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
                    new ServerStubOwnershipAssignment("MatchService", "unknown", "Game0"),
                    new ServerStubOwnershipAssignment("ChatService", "unknown", "Game9"),
                    new ServerStubOwnershipAssignment("LeaderboardService", "unknown", "Game0"),
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
                    Assert.IsType<MatchService>(stub);
                    Assert.True(stub.IsReady);
                    Assert.NotEqual(Guid.Empty, stub.EntityId);
                },
                stub =>
                {
                    Assert.IsType<LeaderboardService>(stub);
                    Assert.True(stub.IsReady);
                    Assert.NotEqual(Guid.Empty, stub.EntityId);
                });
            Assert.Collection(
                runtimeState.ReadyServerStubs,
                stub => Assert.Equal("MatchService", stub.EntityType),
                stub => Assert.Equal("LeaderboardService", stub.EntityType));
            Assert.Collection(
                readyNotifications,
                notification =>
                {
                    Assert.Equal((ulong)7, notification.AssignmentEpoch);
                    Assert.Equal("MatchService", notification.EntityType);
                    Assert.NotEqual(Guid.Empty, notification.EntityId);
                    Assert.True(notification.IsReady);
                },
                notification =>
                {
                    Assert.Equal((ulong)7, notification.AssignmentEpoch);
                    Assert.Equal("LeaderboardService", notification.EntityType);
                    Assert.NotEqual(Guid.Empty, notification.EntityId);
                    Assert.True(notification.IsReady);
                });
            Assert.Single(runtimeState.EntityManager.SnapshotByType<MatchService>());
            Assert.Single(runtimeState.EntityManager.SnapshotByType<LeaderboardService>());
            Assert.Empty(runtimeState.EntityManager.SnapshotByType<ChatService>());
        }

        [Fact]
        public void GameNodeRuntimeState_ReplayingSameLocalOwnership_IsIdempotent()
        {
            List<string> readyNotifications = [];
            GameNodeRuntimeState runtimeState = new("Game0", (_, stub) => readyNotifications.Add(stub.EntityType));

            ServerStubOwnershipSnapshot firstSnapshot = new(
                11,
                [
                    new ServerStubOwnershipAssignment("MatchService", "unknown", "Game0"),
                    new ServerStubOwnershipAssignment("LeaderboardService", "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(firstSnapshot));

            ServerStubEntity firstMatch = runtimeState.OwnedServerStubs[0];
            ServerStubEntity firstLeaderboard = runtimeState.OwnedServerStubs[1];
            Guid firstMatchId = firstMatch.EntityId;
            Guid firstLeaderboardId = firstLeaderboard.EntityId;

            ServerStubOwnershipSnapshot replaySnapshot = new(
                11,
                [
                    new ServerStubOwnershipAssignment("MatchService", firstMatchId.ToString(), "Game0"),
                    new ServerStubOwnershipAssignment("LeaderboardService", firstLeaderboardId.ToString(), "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(replaySnapshot));

            Assert.Same(firstMatch, runtimeState.OwnedServerStubs[0]);
            Assert.Same(firstLeaderboard, runtimeState.OwnedServerStubs[1]);
            Assert.Equal(firstMatchId, runtimeState.OwnedServerStubs[0].EntityId);
            Assert.Equal(firstLeaderboardId, runtimeState.OwnedServerStubs[1].EntityId);
            Assert.Equal(2, runtimeState.EntityManager.Count);
            Assert.Equal(["MatchService", "LeaderboardService"], readyNotifications);
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
                    new ServerStubOwnershipAssignment("MatchService", "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(firstSnapshot));

            ServerStubEntity revokedStub = runtimeState.OwnedServerStubs[0];

            ServerStubOwnershipSnapshot secondSnapshot = new(
                2,
                [
                    new ServerStubOwnershipAssignment("ChatService", "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(secondSnapshot));

            Assert.True(runtimeState.EntityManager.Contains(unrelatedEntity.EntityId));
            Assert.False(runtimeState.EntityManager.Contains(revokedStub.EntityId));
            Assert.Equal(EntityLifecycleState.Destroyed, revokedStub.LifecycleState);
            Assert.Single(runtimeState.OwnedServerStubs);
            Assert.IsType<ChatService>(runtimeState.OwnedServerStubs[0]);
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
                    new ServerStubOwnershipAssignment("MatchService", "unknown", "Game0"),
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
    }

    internal sealed class IndependentEntity : ServerEntity
    {
    }
}
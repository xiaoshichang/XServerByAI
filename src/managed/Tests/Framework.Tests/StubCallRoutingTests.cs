using XServer.Managed.Framework.Entities;
using XServer.Managed.Framework.Runtime;
using XServer.Managed.GameLogic.Services;

namespace XServer.Managed.Framework.Tests
{
    public class StubCallRoutingTests
    {
        [Fact]
        public void ServerEntity_CallStub_PrefersLocalOwnedTarget()
        {
            LoopbackStubCallTransport transport = new();
            GameNodeRuntimeState runtimeState = new("Game0", stubCallTransport: transport);
            transport.Register(runtimeState);

            ServerStubOwnershipSnapshot snapshot = new(
                1,
                [
                    new ServerStubOwnershipAssignment(nameof(OnlineStub), "unknown", "Game0"),
                    new ServerStubOwnershipAssignment(nameof(MatchStub), "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(snapshot));

            OnlineStub onlineStub = Assert.IsType<OnlineStub>(runtimeState.OwnedServerStubs.Single(stub => stub is OnlineStub));
            MatchStub matchStub = Assert.IsType<MatchStub>(runtimeState.OwnedServerStubs.Single(stub => stub is MatchStub));
            byte[] payload = [0x01, 0x02, 0x03];

            onlineStub.CallStub(nameof(MatchStub), 4101u, payload);
            Assert.Equal(0, transport.ForwardCallCount);
            Assert.Collection(
                matchStub.ReceivedCalls,
                call =>
                {
                    Assert.Equal(4101u, call.MsgId);
                    Assert.Equal(payload, call.Payload);
                });
        }

        [Fact]
        public void PlainServerEntity_CallStub_CanTargetOwnedMatchStub()
        {
            LoopbackStubCallTransport transport = new();
            GameNodeRuntimeState runtimeState = new("Game0", stubCallTransport: transport);
            transport.Register(runtimeState);

            ServerStubOwnershipSnapshot snapshot = new(
                5,
                [
                    new ServerStubOwnershipAssignment(nameof(MatchStub), "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(snapshot));

            TestCallerEntity caller = new();
            Assert.Equal(EntityManagerErrorCode.None, runtimeState.EntityManager.Register(caller));
            caller.SetStubCaller(runtimeState);

            MatchStub matchStub = Assert.IsType<MatchStub>(runtimeState.OwnedServerStubs.Single());
            byte[] payload = [0x21, 0x22];

            caller.CallStub(nameof(MatchStub), 4105u, payload);
            Assert.Collection(
                matchStub.ReceivedCalls,
                call =>
                {
                    Assert.Equal(4105u, call.MsgId);
                    Assert.Equal(payload, call.Payload);
                });
        }

        [Fact]
        public void OnlineStub_CallStub_ForwardsToRemoteMatchStub_AndDeliversMessage()
        {
            LoopbackStubCallTransport transport = new();
            GameNodeRuntimeState onlineRuntime = new("Game0", stubCallTransport: transport);
            GameNodeRuntimeState matchRuntime = new("Game1", stubCallTransport: transport);
            transport.Register(onlineRuntime);
            transport.Register(matchRuntime);

            ServerStubOwnershipSnapshot snapshot = new(
                7,
                [
                    new ServerStubOwnershipAssignment(nameof(OnlineStub), "unknown", "Game0"),
                    new ServerStubOwnershipAssignment(nameof(MatchStub), "unknown", "Game1"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, onlineRuntime.ApplyOwnership(snapshot));
            Assert.Equal(GameNodeRuntimeStateErrorCode.None, matchRuntime.ApplyOwnership(snapshot));

            OnlineStub onlineStub = Assert.IsType<OnlineStub>(onlineRuntime.OwnedServerStubs.Single());
            MatchStub matchStub = Assert.IsType<MatchStub>(matchRuntime.OwnedServerStubs.Single());
            byte[] payload = [0xAA, 0xBB, 0xCC];

            onlineStub.CallStub(nameof(MatchStub), 4102u, payload);
            Assert.Equal(1, transport.ForwardCallCount);
            Assert.Equal(nameof(MatchStub), transport.LastTargetStubType);
            Assert.Equal("Game1", transport.LastTargetGameNodeId);
            Assert.NotNull(transport.LastMessage);
            Assert.Equal(4102u, transport.LastMessage!.Value.MsgId);
            Assert.Equal(payload, transport.LastMessage.Value.Payload.ToArray());
            Assert.Collection(
                matchStub.ReceivedCalls,
                call =>
                {
                    Assert.Equal(4102u, call.MsgId);
                    Assert.Equal(payload, call.Payload);
                });
        }

        [Fact]
        public void ServerEntity_CallStub_RejectsUnknownTargetStubType()
        {
            LoopbackStubCallTransport transport = new();
            GameNodeRuntimeState runtimeState = new("Game0", stubCallTransport: transport);
            transport.Register(runtimeState);

            ServerStubOwnershipSnapshot snapshot = new(
                2,
                [
                    new ServerStubOwnershipAssignment(nameof(OnlineStub), "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(snapshot));

            OnlineStub onlineStub = Assert.IsType<OnlineStub>(runtimeState.OwnedServerStubs.Single());

            onlineStub.CallStub(nameof(MatchStub), 4103u, Array.Empty<byte>());

            Assert.Equal(0, transport.ForwardCallCount);
        }

        [Fact]
        public void ServerEntity_CallStub_ReportsTargetNodeUnavailable_WhenTransportCannotReachOwner()
        {
            UnreachableStubCallTransport transport = new();
            GameNodeRuntimeState onlineRuntime = new("Game0", stubCallTransport: transport);

            ServerStubOwnershipSnapshot snapshot = new(
                3,
                [
                    new ServerStubOwnershipAssignment(nameof(OnlineStub), "unknown", "Game0"),
                    new ServerStubOwnershipAssignment(nameof(MatchStub), "unknown", "Game9"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, onlineRuntime.ApplyOwnership(snapshot));

            OnlineStub onlineStub = Assert.IsType<OnlineStub>(onlineRuntime.OwnedServerStubs.Single());

            onlineStub.CallStub(nameof(MatchStub), 4104u, Array.Empty<byte>());

            Assert.Equal(1, transport.ForwardCallCount);
        }
    }

    internal sealed class LoopbackStubCallTransport : IStubCallTransport
    {
        private readonly Dictionary<string, GameNodeRuntimeState> _runtimes = new(StringComparer.Ordinal);

        public int ForwardCallCount { get; private set; }

        public string? LastTargetStubType { get; private set; }

        public string? LastTargetGameNodeId { get; private set; }

        public StubCallMessage? LastMessage { get; private set; }

        public void Register(GameNodeRuntimeState runtimeState)
        {
            _runtimes[runtimeState.NodeId] = runtimeState;
        }

        public StubCallErrorCode Forward(
            string targetStubType,
            string targetGameNodeId,
            StubCallMessage message)
        {
            ForwardCallCount++;
            LastTargetStubType = targetStubType;
            LastTargetGameNodeId = targetGameNodeId;
            LastMessage = message;

            if (!_runtimes.TryGetValue(targetGameNodeId, out GameNodeRuntimeState? runtimeState))
            {
                return StubCallErrorCode.TargetNodeUnavailable;
            }

            return runtimeState.ReceiveStubCall(targetStubType, message);
        }
    }

    internal sealed class UnreachableStubCallTransport : IStubCallTransport
    {
        public int ForwardCallCount { get; private set; }

        public StubCallErrorCode Forward(
            string targetStubType,
            string targetGameNodeId,
            StubCallMessage message)
        {
            _ = targetStubType;
            _ = targetGameNodeId;
            _ = message;
            ForwardCallCount++;
            return StubCallErrorCode.TargetNodeUnavailable;
        }
    }

    internal sealed class TestCallerEntity : ServerEntity
    {
    }
}

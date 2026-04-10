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
            LoopbackMailboxCallTransport transport = new();
            GameNodeRuntimeState runtimeState = new("Game0", mailboxCallTransport: transport);
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
            LoopbackMailboxCallTransport transport = new();
            GameNodeRuntimeState runtimeState = new("Game0", mailboxCallTransport: transport);
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
            LoopbackMailboxCallTransport transport = new();
            GameNodeRuntimeState onlineRuntime = new("Game0", mailboxCallTransport: transport);
            GameNodeRuntimeState matchRuntime = new("Game1", mailboxCallTransport: transport);
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
            Assert.Equal(nameof(MatchStub), transport.LastTargetMailboxName);
            Assert.Equal(string.Empty, transport.LastTargetGameNodeId);
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
            LoopbackMailboxCallTransport transport = new();
            GameNodeRuntimeState runtimeState = new("Game0", mailboxCallTransport: transport);
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
            UnreachableMailboxCallTransport transport = new();
            GameNodeRuntimeState onlineRuntime = new("Game0", mailboxCallTransport: transport);

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

    internal sealed class LoopbackMailboxCallTransport : IMailboxCallTransport
    {
        private readonly Dictionary<string, GameNodeRuntimeState> _runtimes = new(StringComparer.Ordinal);

        public int ForwardCallCount { get; private set; }

        public string? LastTargetMailboxName { get; private set; }

        public string? LastTargetGameNodeId { get; private set; }

        public MailboxCallMessage? LastMessage { get; private set; }

        public void Register(GameNodeRuntimeState runtimeState)
        {
            _runtimes[runtimeState.NodeId] = runtimeState;
        }

        public MailboxCallErrorCode Forward(
            MailboxAddress targetAddress,
            MailboxCallMessage message)
        {
            ForwardCallCount++;
            LastTargetMailboxName = targetAddress.EntityId.ToString("D");
            LastTargetGameNodeId = targetAddress.TargetGameNodeId;
            LastMessage = message;

            if (!_runtimes.TryGetValue(targetAddress.TargetGameNodeId, out GameNodeRuntimeState? runtimeState))
            {
                return MailboxCallErrorCode.TargetNodeUnavailable;
            }

            return runtimeState.ReceiveMailboxCall(targetAddress.EntityId.ToString("D"), message);
        }

        public MailboxCallErrorCode Forward(
            string stubtype,
            MailboxCallMessage message)
        {
            ForwardCallCount++;
            LastTargetMailboxName = stubtype;
            LastTargetGameNodeId = string.Empty;
            LastMessage = message;

            foreach (GameNodeRuntimeState runtimeState in _runtimes.Values)
            {
                MailboxCallErrorCode result = runtimeState.ReceiveMailboxCall(stubtype, message);
                if (result != MailboxCallErrorCode.UnknownTargetMailbox)
                {
                    return result;
                }
            }

            return MailboxCallErrorCode.UnknownTargetMailbox;
        }
    }

    internal sealed class UnreachableMailboxCallTransport : IMailboxCallTransport
    {
        public int ForwardCallCount { get; private set; }

        public MailboxCallErrorCode Forward(
            MailboxAddress targetAddress,
            MailboxCallMessage message)
        {
            _ = targetAddress;
            _ = message;
            ForwardCallCount++;
            return MailboxCallErrorCode.TargetNodeUnavailable;
        }

        public MailboxCallErrorCode Forward(
            string stubtype,
            MailboxCallMessage message)
        {
            _ = stubtype;
            _ = message;
            ForwardCallCount++;
            return MailboxCallErrorCode.TargetNodeUnavailable;
        }
    }

    internal sealed class TestCallerEntity : ServerEntity
    {
    }
}

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
            LoopbackServerEntityMessageTransport transport = new();
            GameNodeRuntimeState runtimeState = new("Game0", messageTransport: transport);
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
            LoopbackServerEntityMessageTransport transport = new();
            GameNodeRuntimeState runtimeState = new("Game0", messageTransport: transport);
            transport.Register(runtimeState);

            ServerStubOwnershipSnapshot snapshot = new(
                5,
                [
                    new ServerStubOwnershipAssignment(nameof(MatchStub), "unknown", "Game0"),
                ]);

            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(snapshot));

            TestCallerEntity caller = new();
            Assert.Equal(EntityManagerErrorCode.None, runtimeState.EntityManager.Register(caller));
            caller.SetMessageSender(runtimeState);

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
        public void PlainServerEntity_CallMailbox_PrefersLocalNonMigratableTarget()
        {
            LoopbackServerEntityMessageTransport transport = new();
            GameNodeRuntimeState runtimeState = new("Game0", messageTransport: transport);
            transport.Register(runtimeState);

            TestCallerEntity caller = new();
            TestMailboxEntity target = new();
            Assert.Equal(EntityManagerErrorCode.None, runtimeState.EntityManager.Register(caller));
            Assert.Equal(EntityManagerErrorCode.None, runtimeState.EntityManager.Register(target));
            caller.SetMessageSender(runtimeState);

            byte[] payload = [0x31, 0x32];

            caller.CallMailbox(new MailboxAddress(target.EntityId, "Game0"), 5106u, payload);

            Assert.Equal(0, transport.ForwardCallCount);
            Assert.Collection(
                target.ReceivedMailboxCalls,
                call =>
                {
                    Assert.Equal(5106u, call.MsgId);
                    Assert.Equal(payload, call.Payload);
                });
        }

        [Fact]
        public void PlainServerEntity_CallMailbox_ForwardsToRemoteNonMigratableTarget()
        {
            LoopbackServerEntityMessageTransport transport = new();
            GameNodeRuntimeState sourceRuntime = new("Game0", messageTransport: transport);
            GameNodeRuntimeState targetRuntime = new("Game1", messageTransport: transport);
            transport.Register(sourceRuntime);
            transport.Register(targetRuntime);

            TestCallerEntity caller = new();
            TestMailboxEntity target = new();
            Assert.Equal(EntityManagerErrorCode.None, sourceRuntime.EntityManager.Register(caller));
            Assert.Equal(EntityManagerErrorCode.None, targetRuntime.EntityManager.Register(target));
            caller.SetMessageSender(sourceRuntime);

            byte[] payload = [0x41, 0x42, 0x43];

            caller.CallMailbox(new MailboxAddress(target.EntityId, "Game1"), 5107u, payload);

            Assert.Equal(1, transport.ForwardCallCount);
            Assert.Equal(target.EntityId.ToString("D"), transport.LastTargetMailboxName);
            Assert.Equal("Game1", transport.LastTargetGameNodeId);
            Assert.Collection(
                target.ReceivedMailboxCalls,
                call =>
                {
                    Assert.Equal(5107u, call.MsgId);
                    Assert.Equal(payload, call.Payload);
                });
        }

        [Fact]
        public void OnlineStub_CallStub_ForwardsToRemoteMatchStub_AndDeliversMessage()
        {
            LoopbackServerEntityMessageTransport transport = new();
            GameNodeRuntimeState onlineRuntime = new("Game0", messageTransport: transport);
            GameNodeRuntimeState matchRuntime = new("Game1", messageTransport: transport);
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
            Assert.NotNull(transport.LastMailboxMessage);
            Assert.Equal(4102u, transport.LastMailboxMessage!.Value.MsgId);
            Assert.Equal(payload, transport.LastMailboxMessage.Value.Payload.ToArray());
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
            LoopbackServerEntityMessageTransport transport = new();
            GameNodeRuntimeState runtimeState = new("Game0", messageTransport: transport);
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
            UnreachableServerEntityMessageTransport transport = new();
            GameNodeRuntimeState onlineRuntime = new("Game0", messageTransport: transport);

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

    internal sealed class LoopbackServerEntityMessageTransport : IServerEntityMessageTransport
    {
        private readonly Dictionary<string, GameNodeRuntimeState> _runtimes = new(StringComparer.Ordinal);

        public int ForwardCallCount { get; private set; }

        public int ServerProxyForwardCallCount { get; private set; }

        public int ClientProxyForwardCallCount { get; private set; }

        public string? LastTargetMailboxName { get; private set; }

        public string? LastTargetGameNodeId { get; private set; }

        public EntityMessage? LastMailboxMessage { get; private set; }

        public Guid LastServerProxyTargetEntityId { get; private set; }

        public string? LastServerProxyRouteGateNodeId { get; private set; }

        public EntityMessage? LastServerProxyMessage { get; private set; }

        public Guid LastClientProxyTargetEntityId { get; private set; }

        public string? LastClientProxyRouteGateNodeId { get; private set; }

        public EntityMessage? LastClientProxyMessage { get; private set; }

        public void Register(GameNodeRuntimeState runtimeState)
        {
            _runtimes[runtimeState.NodeId] = runtimeState;
        }

        public MailboxCallErrorCode ForwardByMailbox(
            MailboxAddress targetAddress,
            EntityMessage message)
        {
            ForwardCallCount++;
            LastTargetMailboxName = targetAddress.EntityId.ToString("D");
            LastTargetGameNodeId = targetAddress.TargetGameNodeId;
            LastMailboxMessage = message;

            if (!_runtimes.TryGetValue(targetAddress.TargetGameNodeId, out GameNodeRuntimeState? runtimeState))
            {
                return MailboxCallErrorCode.TargetNodeUnavailable;
            }

            return runtimeState.ReceiveMailboxCall(targetAddress.EntityId, string.Empty, message);
        }

        public MailboxCallErrorCode ForwardByStubType(
            string stubType,
            EntityMessage message)
        {
            ForwardCallCount++;
            LastTargetMailboxName = stubType;
            LastTargetGameNodeId = string.Empty;
            LastMailboxMessage = message;

            foreach (GameNodeRuntimeState runtimeState in _runtimes.Values)
            {
                MailboxCallErrorCode result = runtimeState.ReceiveMailboxCall(stubType, message);
                if (result != MailboxCallErrorCode.UnknownTargetMailbox)
                {
                    return result;
                }
            }

            return MailboxCallErrorCode.UnknownTargetMailbox;
        }

        public ProxyCallErrorCode ForwardByServerProxy(
            ProxyAddress targetAddress,
            EntityMessage message)
        {
            _ = targetAddress;
            _ = message;
            ServerProxyForwardCallCount++;
            LastServerProxyTargetEntityId = targetAddress.EntityId;
            LastServerProxyRouteGateNodeId = targetAddress.RouteGateNodeId;
            LastServerProxyMessage = message;

            foreach (GameNodeRuntimeState runtimeState in _runtimes.Values)
            {
                if (runtimeState.EntityManager.Contains(targetAddress.EntityId))
                {
                    return runtimeState.ReceiveProxyCall(targetAddress.EntityId, message);
                }
            }

            return ProxyCallErrorCode.UnknownTargetEntity;
        }

        public ProxyCallErrorCode ForwardByClientProxy(
            ProxyAddress targetAddress,
            EntityMessage message)
        {
            ClientProxyForwardCallCount++;
            LastClientProxyTargetEntityId = targetAddress.EntityId;
            LastClientProxyRouteGateNodeId = targetAddress.RouteGateNodeId;
            LastClientProxyMessage = message;
            return ProxyCallErrorCode.None;
        }
    }

    internal sealed class UnreachableServerEntityMessageTransport : IServerEntityMessageTransport
    {
        public int ForwardCallCount { get; private set; }

        public MailboxCallErrorCode ForwardByMailbox(
            MailboxAddress targetAddress,
            EntityMessage message)
        {
            _ = targetAddress;
            _ = message;
            ForwardCallCount++;
            return MailboxCallErrorCode.TargetNodeUnavailable;
        }

        public MailboxCallErrorCode ForwardByStubType(
            string stubType,
            EntityMessage message)
        {
            _ = stubType;
            _ = message;
            ForwardCallCount++;
            return MailboxCallErrorCode.TargetNodeUnavailable;
        }

        public ProxyCallErrorCode ForwardByServerProxy(
            ProxyAddress targetAddress,
            EntityMessage message)
        {
            _ = targetAddress;
            _ = message;
            return ProxyCallErrorCode.TargetNodeUnavailable;
        }

        public ProxyCallErrorCode ForwardByClientProxy(
            ProxyAddress targetAddress,
            EntityMessage message)
        {
            _ = targetAddress;
            _ = message;
            return ProxyCallErrorCode.TargetNodeUnavailable;
        }
    }

    internal sealed class TestCallerEntity : ServerEntity
    {
    }

    internal sealed class TestMailboxEntity : ServerEntity
    {
        private readonly List<ReceivedMailboxCall> _receivedMailboxCalls = [];

        public IReadOnlyList<ReceivedMailboxCall> ReceivedMailboxCalls => _receivedMailboxCalls;

        public override bool IsMigratable()
        {
            return false;
        }

        protected override MailboxCallErrorCode OnMailboxCall(EntityMessage message)
        {
            _receivedMailboxCalls.Add(new ReceivedMailboxCall(message.MsgId, message.Payload.ToArray()));
            return MailboxCallErrorCode.None;
        }

        internal readonly record struct ReceivedMailboxCall(uint MsgId, byte[] Payload);
    }
}

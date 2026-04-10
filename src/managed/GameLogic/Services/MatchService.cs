using XServer.Managed.Framework.Entities;
using XServer.Managed.Framework.Interop;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.GameLogic.Services
{
    public sealed class MatchStub : ServerStubEntity
    {
        private readonly List<ReceivedCall> _receivedCalls = [];

        public IReadOnlyList<ReceivedCall> ReceivedCalls => _receivedCalls;

        protected override StubCallErrorCode OnStubCall(EntityMessage message)
        {
            NativeLoggerBridge.Info(nameof(MatchStub), $"MatchStub received call msgId={message.MsgId}.");
            _receivedCalls.Add(new ReceivedCall(message.MsgId, message.Payload.ToArray()));
            return StubCallErrorCode.None;
        }

        public readonly record struct ReceivedCall(uint MsgId, byte[] Payload);
    }
}

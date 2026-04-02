using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Runtime
{
    public interface IStubCallTransport
    {
        StubCallErrorCode Forward(
            string targetStubType,
            string targetGameNodeId,
            StubCallMessage message);
    }
}

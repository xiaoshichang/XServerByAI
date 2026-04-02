using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Runtime
{
    internal interface IServerStubCaller
    {
        void CallStub(ServerEntity sourceEntity, string targetStubType, StubCallMessage message);
    }
}

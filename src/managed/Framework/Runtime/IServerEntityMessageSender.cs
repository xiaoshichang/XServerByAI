using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Runtime
{
    internal interface IServerEntityMessageSender
    {
        void CallStub(ServerEntity sourceEntity, string targetStubType, StubCallMessage message);

        void CallMailbox(ServerEntity sourceEntity, MailboxAddress targetAddress, MailboxCallMessage message);

        void CallServerProxy(ServerEntity sourceEntity, ProxyAddress targetAddress, ProxyCallMessage message);

        void CallClient(ServerEntity sourceEntity, ProxyAddress targetAddress, ProxyCallMessage message);
    }
}

using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Runtime
{
    internal interface IServerEntityMessageSender
    {
        void CallStub(ServerEntity sourceEntity, string targetStubType, EntityMessage message);

        void CallMailbox(ServerEntity sourceEntity, MailboxAddress targetAddress, EntityMessage message);

        void CallServerProxy(ServerEntity sourceEntity, ProxyAddress targetAddress, EntityMessage message);

        void CallClient(ServerEntity sourceEntity, ProxyAddress targetAddress, EntityMessage message);
    }
}

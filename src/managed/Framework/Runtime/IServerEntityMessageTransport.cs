using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Runtime
{
    public interface IServerEntityMessageTransport
    {
        MailboxCallErrorCode ForwardByMailbox(
            MailboxAddress targetAddress,
            EntityMessage message);

        MailboxCallErrorCode ForwardByStubType(
            string stubType,
            EntityMessage message);

        ProxyCallErrorCode ForwardByServerProxy(
            ProxyAddress targetAddress,
            EntityMessage message);

        ProxyCallErrorCode ForwardByClientProxy(
            ProxyAddress targetAddress,
            EntityMessage message);
    }
}

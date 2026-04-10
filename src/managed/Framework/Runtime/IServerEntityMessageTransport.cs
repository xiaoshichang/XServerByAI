using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Runtime
{
    public interface IServerEntityMessageTransport
    {
        MailboxCallErrorCode ForwardByMailbox(
            MailboxAddress targetAddress,
            MailboxCallMessage message);

        MailboxCallErrorCode ForwardByStubType(
            string stubType,
            MailboxCallMessage message);

        ProxyCallErrorCode ForwardByServerProxy(
            ProxyAddress targetAddress,
            ProxyCallMessage message);

        ProxyCallErrorCode ForwardByClientProxy(
            ProxyAddress targetAddress,
            ProxyCallMessage message);
    }
}

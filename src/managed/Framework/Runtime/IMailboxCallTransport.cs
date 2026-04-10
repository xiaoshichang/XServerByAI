using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Runtime
{
    public interface IMailboxCallTransport
    {
        MailboxCallErrorCode Forward(
            MailboxAddress targetAddress,
            MailboxCallMessage message);

        MailboxCallErrorCode Forward(
            string stubtype,
            MailboxCallMessage message);
    }
}

namespace XServer.Managed.Framework.Runtime
{
    public enum MailboxCallErrorCode
    {
        None = 0,
        InvalidArgument = 1,
        InvalidMessageId = 2,
        UnknownTargetMailbox = 3,
        TargetNodeUnavailable = 4,
        MailboxRejected = 5,
    }

    public static class MailboxCallError
    {
        public static string Message(MailboxCallErrorCode code)
        {
            return code switch
            {
                MailboxCallErrorCode.None => "No error.",
                MailboxCallErrorCode.InvalidArgument => "Mailbox call argument is invalid.",
                MailboxCallErrorCode.InvalidMessageId => "Mailbox call msgId must not be zero.",
                MailboxCallErrorCode.UnknownTargetMailbox => "Mailbox target is unknown or has no owner.",
                MailboxCallErrorCode.TargetNodeUnavailable => "Mailbox target node is unavailable.",
                MailboxCallErrorCode.MailboxRejected => "Target mailbox rejected the incoming call.",
                _ => "Unknown mailbox call error.",
            };
        }
    }
}

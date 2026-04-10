namespace XServer.Managed.Framework.Entities
{
    public readonly record struct MailboxCallMessage(uint MsgId, ReadOnlyMemory<byte> Payload);
}

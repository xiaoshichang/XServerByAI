namespace XServer.Managed.Framework.Entities
{
    public readonly record struct StubCallMessage(uint MsgId, ReadOnlyMemory<byte> Payload);
}

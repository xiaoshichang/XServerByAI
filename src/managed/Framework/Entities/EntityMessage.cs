namespace XServer.Managed.Framework.Entities
{
    public readonly record struct EntityMessage(uint MsgId, ReadOnlyMemory<byte> Payload);
}

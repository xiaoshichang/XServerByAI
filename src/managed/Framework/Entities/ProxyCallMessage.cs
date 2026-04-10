namespace XServer.Managed.Framework.Entities
{
    public readonly record struct ProxyCallMessage(uint MsgId, ReadOnlyMemory<byte> Payload);
}

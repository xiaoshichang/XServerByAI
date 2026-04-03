namespace XServer.Client.Protocol;

public readonly record struct KcpSegment(
    uint Conversation,
    KcpCommand Command,
    byte Fragment,
    ushort Window,
    uint Timestamp,
    uint SequenceNumber,
    uint UnacknowledgedSequenceNumber,
    ReadOnlyMemory<byte> Payload);

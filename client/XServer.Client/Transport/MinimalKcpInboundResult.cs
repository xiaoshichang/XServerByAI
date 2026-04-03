namespace XServer.Client.Transport;

public sealed record MinimalKcpInboundResult(
    IReadOnlyList<byte[]> OutgoingDatagrams,
    IReadOnlyList<ReadOnlyMemory<byte>> ReceivedPayloads,
    IReadOnlyList<string> TraceMessages);

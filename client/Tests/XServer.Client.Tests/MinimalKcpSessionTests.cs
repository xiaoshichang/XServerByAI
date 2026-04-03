using XServer.Client.Configuration;
using XServer.Client.Transport;

namespace XServer.Client.Tests;

public sealed class MinimalKcpSessionTests
{
    private static readonly KcpTransportOptions KcpOptions = new(
        Mtu: 1200,
        SendWindow: 128,
        ReceiveWindow: 128,
        NoDelay: true,
        IntervalMs: 10,
        FastResend: 2,
        NoCongestionWindow: false,
        MinRtoMs: 30,
        DeadLinkCount: 20,
        StreamMode: false);

    [Fact]
    public void QueuePayloadProducesOutboundDatagram()
    {
        MinimalKcpSession session = new(11U, KcpOptions);

        bool success = session.TryQueuePayload(
            new byte[] { 0x11, 0x22, 0x33 },
            1234U,
            out IReadOnlyList<byte[]> datagrams,
            out string? error);

        Assert.True(success);
        Assert.Null(error);
        Assert.Single(datagrams);
        Assert.Equal(1, session.PendingAcknowledgementCount);
        Assert.Equal(1U, session.NextSendSequence);
    }

    [Fact]
    public void TwoSessionsExchangePayloadAndAck()
    {
        MinimalKcpSession sender = new(11U, KcpOptions);
        MinimalKcpSession receiver = new(11U, KcpOptions);
        byte[] payload = "gate-client-first-payload"u8.ToArray();

        Assert.True(sender.TryQueuePayload(payload, 100U, out IReadOnlyList<byte[]> outboundDatagrams, out string? sendError));
        Assert.Null(sendError);

        Assert.True(
            receiver.TryProcessInboundDatagram(outboundDatagrams[0], 110U, out MinimalKcpInboundResult inboundResult, out string? receiveError));
        Assert.Null(receiveError);
        Assert.Single(inboundResult.OutgoingDatagrams);
        Assert.Single(inboundResult.ReceivedPayloads);
        Assert.Equal(payload, inboundResult.ReceivedPayloads[0].ToArray());
        Assert.Equal(1U, receiver.NextReceiveSequence);

        Assert.True(
            sender.TryProcessInboundDatagram(inboundResult.OutgoingDatagrams[0], 120U, out MinimalKcpInboundResult ackResult, out string? ackError));
        Assert.Null(ackError);
        Assert.Empty(ackResult.ReceivedPayloads);
        Assert.Equal(0, sender.PendingAcknowledgementCount);
    }
}

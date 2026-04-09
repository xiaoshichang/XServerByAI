namespace XServer.Client.Configuration;

public sealed record KcpTransportOptions(
    int Mtu,
    int SendWindow,
    int ReceiveWindow,
    bool NoDelay,
    int IntervalMs,
    int FastResend,
    bool NoCongestionWindow,
    int MinRtoMs,
    int DeadLinkCount,
    bool StreamMode)
{
    public int MaxDatagramPayloadBytes => Math.Max(1, Mtu - Protocol.KcpDatagramCodec.KcpOverheadBytes);
}

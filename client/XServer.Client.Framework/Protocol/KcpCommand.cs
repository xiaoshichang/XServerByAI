namespace XServer.Client.Protocol;

public enum KcpCommand : byte
{
    Push = 81,
    Ack = 82,
    WindowProbeAsk = 83,
    WindowProbeTell = 84,
}

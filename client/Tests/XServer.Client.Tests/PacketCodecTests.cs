using System.Text;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.Tests;

public sealed class PacketCodecTests
{
    [Fact]
    public void EncodeAndDecodeRoundTrip()
    {
        byte[] payload = Encoding.UTF8.GetBytes("hello-client");
        PacketHeader header = PacketCodec.CreateHeader(45001U, 7U, PacketFlags.Response, (uint)payload.Length);

        Assert.Equal(PacketCodecErrorCode.None, PacketCodec.GetWireSize(payload.Length, out int wireSize));

        byte[] buffer = GC.AllocateUninitializedArray<byte>(wireSize);
        Assert.Equal(PacketCodecErrorCode.None, PacketCodec.EncodePacket(header, payload, buffer));
        Assert.Equal(PacketCodecErrorCode.None, PacketCodec.DecodePacket(buffer, out PacketView packet));

        Assert.Equal(PacketConstants.Magic, packet.Header.Magic);
        Assert.Equal(PacketConstants.Version, packet.Header.Version);
        Assert.Equal(PacketFlags.Response, packet.Header.Flags);
        Assert.Equal((uint)payload.Length, packet.Header.Length);
        Assert.Equal(45001U, packet.Header.MsgId);
        Assert.Equal(7U, packet.Header.Seq);
        Assert.Equal(payload, packet.Payload.ToArray());
    }

    [Fact]
    public void DecodeRejectsLengthMismatch()
    {
        PacketHeader header = PacketCodec.CreateHeader(45050U, 1U, PacketFlags.None, 4U);
        byte[] buffer = new byte[PacketConstants.HeaderSize + 2];

        Assert.Equal(PacketCodecErrorCode.None, PacketCodec.WriteHeader(header, buffer));
        Assert.Equal(PacketCodecErrorCode.LengthMismatch, PacketCodec.DecodePacket(buffer, out _));
    }
}

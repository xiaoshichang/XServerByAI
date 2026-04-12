using XServer.Client.Entities;
using XServer.Client.Runtime;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.Rpc;

public sealed class ClientEntityRpcPacketSender : IClientEntityRpcSender
{
    private readonly ClientRuntimeState _state;
    private readonly Action<PacketHeader, ReadOnlyMemory<byte>> _sendPacket;

    public ClientEntityRpcPacketSender(
        ClientRuntimeState state,
        Action<PacketHeader, ReadOnlyMemory<byte>> sendPacket)
    {
        _state = state ?? throw new ArgumentNullException(nameof(state));
        _sendPacket = sendPacket ?? throw new ArgumentNullException(nameof(sendPacket));
    }

    public void SendServerRpc(ClientEntity sourceEntity, ClientEntityRpcRequest request)
    {
        ArgumentNullException.ThrowIfNull(sourceEntity);
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(request.Payload);

        PacketHeader header = PacketCodec.CreateHeader(
            request.MsgId,
            _state.AllocatePacketSequence(),
            PacketFlags.None,
            checked((uint)request.Payload.Length));

        _sendPacket(header, request.Payload);
        _state.RecordSentPacket(header);
    }
}

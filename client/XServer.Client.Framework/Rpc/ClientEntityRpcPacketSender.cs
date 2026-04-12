using XServer.Client.Entities;
using XServer.Client.Runtime;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.Rpc;

public sealed class ClientEntityRpcPacketSender : IClientEntityRpcSender
{
    private readonly GameInstance _gameInstance;
    private readonly Action<PacketHeader, ReadOnlyMemory<byte>> _sendPacket;

    public ClientEntityRpcPacketSender(
        GameInstance gameInstance,
        Action<PacketHeader, ReadOnlyMemory<byte>> sendPacket)
    {
        _gameInstance = gameInstance ?? throw new ArgumentNullException(nameof(gameInstance));
        _sendPacket = sendPacket ?? throw new ArgumentNullException(nameof(sendPacket));
    }

    public void SendServerRpc(ClientEntity sourceEntity, ClientEntityRpcRequest request)
    {
        ArgumentNullException.ThrowIfNull(sourceEntity);
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(request.Payload);

        PacketHeader header = PacketCodec.CreateHeader(
            request.MsgId,
            _gameInstance.AllocatePacketSequence(),
            PacketFlags.None,
            checked((uint)request.Payload.Length));

        _sendPacket(header, request.Payload);
        _gameInstance.RecordSentPacket(header);
    }
}

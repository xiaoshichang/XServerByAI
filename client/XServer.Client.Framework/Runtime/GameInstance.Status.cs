namespace XServer.Client.Runtime;

public sealed partial class GameInstance
{
    public string BuildStatusText(int pendingAckCount, uint nextKcpSendSequence, uint nextKcpReceiveSequence)
    {
        List<string> lines =
        [
            $"Lifecycle: {LifecycleState}",
            $"Connected: {IsConnected}",
            $"Remote: {(Profile is null ? "<none>" : $"{Profile.DisplayEndpoint} ({Profile.GateNodeId}, {Profile.EndpointSource})")}",
            $"Local: {LocalEndpointText ?? "<none>"}",
            $"Conversation: {Profile?.Conversation.ToString() ?? "<none>"}",
            $"PacketSeq.Next: {NextPacketSequence}",
            $"Packets: sent={SentPacketCount}, received={ReceivedPacketCount}",
            $"Entities: total={EntityManager.Count}",
            $"KCP: pendingAck={pendingAckCount}, nextSendSn={nextKcpSendSequence}, nextRecvSn={nextKcpReceiveSequence}",
            $"LastSentAt: {LastSentAt?.ToString("O") ?? "<none>"}",
            $"LastReceivedAt: {LastReceivedAt?.ToString("O") ?? "<none>"}",
        ];

        if (Account is null)
        {
            lines.Add("Account: <none>");
        }
        else
        {
            lines.Add(
                $"Account: id={Account.AccountId}, cached={(HasCachedLoginGrant ? LastLoginProfile!.DisplayEndpoint : "<none>")}, " +
                $"issuedAt={LastLoginIssuedAt?.ToString("O") ?? "<none>"}, expiresAt={LastLoginExpiresAt?.ToString("O") ?? "<none>"}");
        }

        if (!AvatarSession.HasSelection)
        {
            lines.Add("AvatarSession: <none>");
        }
        else
        {
            lines.Add(
                $"AvatarSession: entityId={AvatarSession.SelectedAvatarEntityId!.Value:D}, " +
                $"confirmed={AvatarSession.IsSelectionConfirmed}, " +
                $"game={AvatarSession.GameNodeId ?? "<unknown>"}, " +
                $"sessionId={AvatarSession.SessionId?.ToString() ?? "<unknown>"}");
        }

        if (Avatar is null)
        {
            lines.Add("Avatar: <none>");
        }
        else
        {
            string currentWeapon = string.IsNullOrWhiteSpace(Avatar.Weapon)
                ? "<none>"
                : Avatar.Weapon;
            lines.Add(
                $"Avatar: id={Avatar.AvatarId}, account={Avatar.AccountId}, " +
                $"pos=({Avatar.PositionX}, {Avatar.PositionY}, {Avatar.PositionZ}), weapon={currentWeapon}");
        }

        return string.Join(Environment.NewLine, lines);
    }
}

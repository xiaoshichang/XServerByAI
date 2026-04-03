using XServer.Client.Configuration;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.Runtime;

public sealed class ClientRuntimeState
{
    public ClientLifecycleState LifecycleState { get; private set; } = ClientLifecycleState.Disconnected;
    public ResolvedClientProfile? Profile { get; private set; }
    public string? LocalEndpointText { get; private set; }
    public string? PlayerName { get; private set; }
    public string? LoginToken { get; private set; }
    public AvatarView? Avatar { get; private set; }
    public uint NextPacketSequence { get; private set; } = 1U;
    public int SentPacketCount { get; private set; }
    public int ReceivedPacketCount { get; private set; }
    public DateTimeOffset? LastSentAt { get; private set; }
    public DateTimeOffset? LastReceivedAt { get; private set; }

    public bool IsConnected => LifecycleState != ClientLifecycleState.Disconnected;
    public bool HasAvatar => Avatar is not null;

    public void MarkConnected(ResolvedClientProfile profile, string? localEndpointText)
    {
        Profile = profile;
        LocalEndpointText = localEndpointText;
        LifecycleState = ClientLifecycleState.Connected;
        SentPacketCount = 0;
        ReceivedPacketCount = 0;
        LastSentAt = null;
        LastReceivedAt = null;
        NextPacketSequence = 1U;
        PlayerName = null;
        LoginToken = null;
        Avatar = null;
    }

    public void MarkDisconnected()
    {
        Profile = null;
        LocalEndpointText = null;
        LifecycleState = ClientLifecycleState.Disconnected;
        PlayerName = null;
        LoginToken = null;
        Avatar = null;
        NextPacketSequence = 1U;
        SentPacketCount = 0;
        ReceivedPacketCount = 0;
        LastSentAt = null;
        LastReceivedAt = null;
    }

    public uint AllocatePacketSequence()
    {
        uint sequence = NextPacketSequence == 0U ? 1U : NextPacketSequence;
        NextPacketSequence = sequence + 1U;
        if (NextPacketSequence == 0U)
        {
            NextPacketSequence = 1U;
        }

        return sequence;
    }

    public void RecordSentPacket(PacketHeader header)
    {
        SentPacketCount++;
        LastSentAt = DateTimeOffset.UtcNow;
    }

    public void RecordReceivedPacket(PacketHeader header)
    {
        ReceivedPacketCount++;
        LastReceivedAt = DateTimeOffset.UtcNow;
    }

    public void MarkLoginStarted(string playerName, string token)
    {
        EnsureConnected();
        PlayerName = playerName;
        LoginToken = token;
        LifecycleState = ClientLifecycleState.LoginPending;
    }

    public void MarkLoginSucceeded(long playerId, string? avatarId = null)
    {
        EnsureConnected();

        string resolvedPlayerName = PlayerName ?? "unknown-player";
        Avatar = new AvatarView
        {
            AvatarId = avatarId ?? $"avatar:{resolvedPlayerName}",
            PlayerId = playerId,
            DisplayName = resolvedPlayerName,
        };
        LifecycleState = ClientLifecycleState.AvatarReady;
    }

    public void UpdateAvatarPosition(float x, float y, float z)
    {
        EnsureAvatarReady();
        Avatar!.PositionX = x;
        Avatar.PositionY = y;
        Avatar.PositionZ = z;
    }

    public void AddWeapon(string weaponId, int count)
    {
        EnsureAvatarReady();
        Avatar!.WeaponInventory.TryGetValue(weaponId, out int existingCount);
        Avatar.WeaponInventory[weaponId] = existingCount + count;
    }

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
            $"KCP: pendingAck={pendingAckCount}, nextSendSn={nextKcpSendSequence}, nextRecvSn={nextKcpReceiveSequence}",
            $"LastSentAt: {LastSentAt?.ToString("O") ?? "<none>"}",
            $"LastReceivedAt: {LastReceivedAt?.ToString("O") ?? "<none>"}",
            $"Login: player={PlayerName ?? "<none>"}, token={(string.IsNullOrEmpty(LoginToken) ? "<none>" : "<set>")}",
        ];

        if (Avatar is null)
        {
            lines.Add("Avatar: <none>");
        }
        else
        {
            string weapons = Avatar.WeaponInventory.Count == 0
                ? "<empty>"
                : string.Join(", ", Avatar.WeaponInventory.Select(entry => $"{entry.Key}x{entry.Value}"));
            lines.Add(
                $"Avatar: id={Avatar.AvatarId}, playerId={Avatar.PlayerId}, name={Avatar.DisplayName}, " +
                $"pos=({Avatar.PositionX}, {Avatar.PositionY}, {Avatar.PositionZ}), weapons={weapons}");
        }

        return string.Join(Environment.NewLine, lines);
    }

    private void EnsureConnected()
    {
        if (!IsConnected)
        {
            throw new InvalidOperationException("The simulated client is not connected.");
        }
    }

    private void EnsureAvatarReady()
    {
        if (LifecycleState != ClientLifecycleState.AvatarReady || Avatar is null)
        {
            throw new InvalidOperationException(
                "The simulated client does not have a ready avatar yet. Send login with localSuccess=true first.");
        }
    }
}

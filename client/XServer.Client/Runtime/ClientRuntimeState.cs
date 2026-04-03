using System.IO;
using XServer.Client.Configuration;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.Runtime;

public sealed class ClientRuntimeState
{
    public ClientLifecycleState LifecycleState { get; private set; } = ClientLifecycleState.Disconnected;
    public ResolvedClientProfile? Profile { get; private set; }
    public string? LocalEndpointText { get; private set; }
    public string? LastLoginAccount { get; private set; }
    public ResolvedClientProfile? LastLoginProfile { get; private set; }
    public DateTimeOffset? LastLoginIssuedAt { get; private set; }
    public DateTimeOffset? LastLoginExpiresAt { get; private set; }
    public AvatarView? Avatar { get; private set; }
    public uint NextPacketSequence { get; private set; } = 1U;
    public int SentPacketCount { get; private set; }
    public int ReceivedPacketCount { get; private set; }
    public DateTimeOffset? LastSentAt { get; private set; }
    public DateTimeOffset? LastReceivedAt { get; private set; }

    public bool IsConnected => LifecycleState != ClientLifecycleState.Disconnected && Profile is not null;
    public bool HasAvatar => Avatar is not null;
    public bool HasCachedLoginGrant => LastLoginProfile is not null;

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
    }

    public void MarkDisconnected()
    {
        Profile = null;
        LocalEndpointText = null;
        LifecycleState = ClientLifecycleState.Disconnected;
        Avatar = null;
        NextPacketSequence = 1U;
        SentPacketCount = 0;
        ReceivedPacketCount = 0;
        LastSentAt = null;
        LastReceivedAt = null;
    }

    public void StoreLoginGrant(
        string account,
        ResolvedClientProfile profile,
        DateTimeOffset issuedAt,
        DateTimeOffset expiresAt)
    {
        LastLoginAccount = account;
        LastLoginProfile = profile;
        LastLoginIssuedAt = issuedAt;
        LastLoginExpiresAt = expiresAt;
    }

    public bool TryGetCachedLoginProfile(string configPath, string gateNodeId, out ResolvedClientProfile profile)
    {
        if (LastLoginProfile is not null &&
            string.Equals(Path.GetFullPath(LastLoginProfile.ConfigPath), Path.GetFullPath(configPath), StringComparison.OrdinalIgnoreCase) &&
            string.Equals(LastLoginProfile.GateNodeId, gateNodeId, StringComparison.Ordinal))
        {
            profile = LastLoginProfile;
            return true;
        }

        profile = null!;
        return false;
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

    public void MarkLocalAvatarReady(long playerId, string? avatarId = null)
    {
        string resolvedLoginAccount = LastLoginAccount ?? "unknown-player";
        Avatar = new AvatarView
        {
            AvatarId = avatarId ?? $"avatar:{resolvedLoginAccount}",
            PlayerId = playerId,
            DisplayName = resolvedLoginAccount,
        };
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
            $"Auth: account={LastLoginAccount ?? "<none>"}, cached={(HasCachedLoginGrant ? LastLoginProfile!.DisplayEndpoint : "<none>")}, expiresAt={LastLoginExpiresAt?.ToString("O") ?? "<none>"}",
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

    private void EnsureAvatarReady()
    {
        if (Avatar is null)
        {
            throw new InvalidOperationException(
                "The simulated client does not have a local Avatar yet. Run login with localSuccess=true first.");
        }
    }
}
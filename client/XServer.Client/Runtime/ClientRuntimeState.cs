using System.IO;
using XServer.Client.Configuration;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.Runtime;

public sealed class ClientRuntimeState
{
    public ClientLifecycleState LifecycleState { get; private set; } = ClientLifecycleState.Disconnected;
    public ResolvedClientProfile? Profile { get; private set; }
    public string? LocalEndpointText { get; private set; }
    public AccountView? Account { get; private set; }
    public string? LastLoginAccount => Account?.AccountId;
    public ResolvedClientProfile? LastLoginProfile => Account?.LastLoginProfile;
    public DateTimeOffset? LastLoginIssuedAt => Account?.LastLoginIssuedAt;
    public DateTimeOffset? LastLoginExpiresAt => Account?.LastLoginExpiresAt;
    public AvatarView? Avatar => Account?.Avatar;
    public uint NextPacketSequence { get; private set; } = 1U;
    public int SentPacketCount { get; private set; }
    public int ReceivedPacketCount { get; private set; }
    public DateTimeOffset? LastSentAt { get; private set; }
    public DateTimeOffset? LastReceivedAt { get; private set; }

    public bool IsConnected => Profile is not null;
    public bool HasAccount => Account is not null;
    public bool HasAvatar => Avatar is not null;
    public bool HasCachedLoginGrant => Account?.HasCachedLoginGrant ?? false;

    public void MarkConnected(ResolvedClientProfile profile, string? localEndpointText)
    {
        Profile = profile;
        LocalEndpointText = localEndpointText;
        SentPacketCount = 0;
        ReceivedPacketCount = 0;
        LastSentAt = null;
        LastReceivedAt = null;
        NextPacketSequence = 1U;
        RefreshLifecycleState();
    }

    public void MarkDisconnected()
    {
        Profile = null;
        LocalEndpointText = null;
        NextPacketSequence = 1U;
        SentPacketCount = 0;
        ReceivedPacketCount = 0;
        LastSentAt = null;
        LastReceivedAt = null;
        RefreshLifecycleState();
    }

    public void StoreLoginGrant(
        string account,
        ResolvedClientProfile profile,
        DateTimeOffset issuedAt,
        DateTimeOffset expiresAt)
    {
        if (Account is null || !string.Equals(Account.AccountId, account, StringComparison.Ordinal))
        {
            Account = new AccountView
            {
                AccountId = account,
            };
        }

        Account.StoreLoginGrant(profile, issuedAt, expiresAt);
        RefreshLifecycleState();
    }

    public bool TryGetCachedLoginProfile(string configPath, out ResolvedClientProfile profile)
    {
        if (LastLoginProfile is not null &&
            string.Equals(Path.GetFullPath(LastLoginProfile.ConfigPath), Path.GetFullPath(configPath), StringComparison.OrdinalIgnoreCase))
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

    public void MarkLocalAvatarReady(string? avatarId = null, string? avatarName = null)
    {
        EnsureAccountReady();

        string accountId = Account!.AccountId;
        string resolvedAvatarId = avatarId ?? $"avatar:{accountId}";
        Account.BindAvatar(new AvatarView
        {
            AccountId = accountId,
            AvatarId = resolvedAvatarId,
            DisplayName = avatarName ?? resolvedAvatarId,
        });
        RefreshLifecycleState();
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
        ];

        if (Account is null)
        {
            lines.Add("Account: <none>");
        }
        else
        {
            lines.Add(
                $"Account: id={Account.AccountId}, cached={(HasCachedLoginGrant ? LastLoginProfile!.DisplayEndpoint : "<none>")}, " +
                $"issuedAt={LastLoginIssuedAt?.ToString("O") ?? "<none>"}, expiresAt={LastLoginExpiresAt?.ToString("O") ?? "<none>"}, " +
                $"avatar={(Account.Avatar?.AvatarId ?? "<none>")}");
        }

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
                $"Avatar: id={Avatar.AvatarId}, account={Avatar.AccountId}, name={Avatar.DisplayName}, " +
                $"pos=({Avatar.PositionX}, {Avatar.PositionY}, {Avatar.PositionZ}), weapons={weapons}");
        }

        return string.Join(Environment.NewLine, lines);
    }

    private void EnsureAccountReady()
    {
        if (Account is null)
        {
            throw new InvalidOperationException(
                "The simulated client does not have a local Account yet. Run login <url> <account> <password> first.");
        }
    }

    private void EnsureAvatarReady()
    {
        if (Avatar is null)
        {
            throw new InvalidOperationException(
                "The simulated client does not have a local Avatar bound to an Account yet. Run login <url> <account> <password> first.");
        }
    }

    private void RefreshLifecycleState()
    {
        if (Avatar is not null)
        {
            LifecycleState = ClientLifecycleState.AvatarReady;
            return;
        }

        if (Account is not null)
        {
            LifecycleState = ClientLifecycleState.LoggedIn;
            return;
        }

        if (Profile is not null)
        {
            LifecycleState = ClientLifecycleState.Connected;
            return;
        }

        LifecycleState = ClientLifecycleState.Disconnected;
    }
}

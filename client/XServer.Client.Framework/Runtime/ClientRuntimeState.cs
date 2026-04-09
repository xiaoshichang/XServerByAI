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
    public bool HasConfirmedAvatar => Avatar?.IsServerConfirmed ?? false;
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

    public AvatarView CreateTemporaryAvatarSelection()
    {
        EnsureAccountReady();

        string accountId = Account!.AccountId;
        string avatarId = Guid.NewGuid().ToString("D");
        string avatarName = BuildTemporaryAvatarName(avatarId);
        return new AvatarView
        {
            AccountId = accountId,
            AvatarId = avatarId,
            DisplayName = avatarName,
        };
    }

    public AvatarView SelectAvatar()
    {
        AvatarView avatar = CreateTemporaryAvatarSelection();
        SelectAvatar(avatar);
        return avatar;
    }

    public void SelectAvatar(AvatarView avatar)
    {
        ArgumentNullException.ThrowIfNull(avatar);
        EnsureAccountReady();

        if (!string.Equals(Account!.AccountId, avatar.AccountId, StringComparison.Ordinal))
        {
            throw new ArgumentException("Avatar selection account does not match the current Account.", nameof(avatar));
        }

        if (string.IsNullOrWhiteSpace(avatar.AvatarId))
        {
            throw new ArgumentException("Avatar selection id must not be empty.", nameof(avatar));
        }

        if (string.IsNullOrWhiteSpace(avatar.DisplayName))
        {
            throw new ArgumentException("Avatar selection display name must not be empty.", nameof(avatar));
        }

        Account.BindAvatar(avatar);
        RefreshLifecycleState();
    }

    public bool ConfirmAvatarSelection(string accountId, string avatarId, string? avatarName = null)
    {
        EnsureAccountReady();

        AvatarView? currentAvatar = Avatar;
        if (currentAvatar is null ||
            !string.Equals(Account!.AccountId, accountId, StringComparison.Ordinal) ||
            !string.Equals(currentAvatar.AvatarId, avatarId, StringComparison.Ordinal))
        {
            return false;
        }

        if (!string.IsNullOrWhiteSpace(avatarName) &&
            !string.Equals(currentAvatar.DisplayName, avatarName, StringComparison.Ordinal))
        {
            Account.BindAvatar(new AvatarView
            {
                AccountId = currentAvatar.AccountId,
                AvatarId = currentAvatar.AvatarId,
                DisplayName = avatarName,
                PositionX = currentAvatar.PositionX,
                PositionY = currentAvatar.PositionY,
                PositionZ = currentAvatar.PositionZ,
            });

            foreach (KeyValuePair<string, int> entry in currentAvatar.WeaponInventory)
            {
                Account.Avatar!.WeaponInventory[entry.Key] = entry.Value;
            }
        }

        Account.Avatar!.MarkServerConfirmed();
        RefreshLifecycleState();
        return true;
    }

    public void ClearAvatarSelection()
    {
        if (Account is null)
        {
            return;
        }

        Account.ClearAvatar();
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
            string avatarConfirmedText = Account.Avatar is null
                ? "<none>"
                : Account.Avatar.IsServerConfirmed.ToString();
            lines.Add(
                $"Account: id={Account.AccountId}, cached={(HasCachedLoginGrant ? LastLoginProfile!.DisplayEndpoint : "<none>")}, " +
                $"issuedAt={LastLoginIssuedAt?.ToString("O") ?? "<none>"}, expiresAt={LastLoginExpiresAt?.ToString("O") ?? "<none>"}, " +
                $"avatarSelection={(Account.Avatar?.AvatarId ?? "<waiting>")}, " +
                $"avatarConfirmed={avatarConfirmedText}");
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
                $"confirmed={Avatar.IsServerConfirmed}, pos=({Avatar.PositionX}, {Avatar.PositionY}, {Avatar.PositionZ}), weapons={weapons}");
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
                "The simulated client does not have a local Avatar bound to an Account yet. Run login <url> <account> <password> and then selectAvatar first.");
        }

        if (!Avatar.IsServerConfirmed)
        {
            throw new InvalidOperationException(
                "The local Avatar selection is still waiting for server confirmation. Wait for the selectAvatar success response first.");
        }
    }

    private void RefreshLifecycleState()
    {
        if (Avatar is not null && Avatar.IsServerConfirmed)
        {
            LifecycleState = ClientLifecycleState.AvatarReady;
            return;
        }

        if (Avatar is not null)
        {
            LifecycleState = ClientLifecycleState.AvatarSelecting;
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

    private static string BuildTemporaryAvatarName(string avatarId)
    {
        int suffixLength = Math.Min(8, avatarId.Length);
        return $"TempAvatar-{avatarId[..suffixLength]}";
    }
}

using System.IO;
using XServer.Client.Configuration;
using XServer.Client.Entities;
using XServer.Client.Rpc;
using XServer.Managed.Foundation.Protocol;
using XServer.Managed.Foundation.Rpc;

namespace XServer.Client.Runtime;

public sealed class ClientRuntimeState
{
    private IClientEntityRpcSender? _rpcSender;

    public ClientLifecycleState LifecycleState { get; private set; } = ClientLifecycleState.Disconnected;
    public ResolvedClientProfile? Profile { get; private set; }
    public string? LocalEndpointText { get; private set; }
    public AccountView? Account { get; private set; }
    public string? LastLoginAccount => Account?.AccountId;
    public ResolvedClientProfile? LastLoginProfile => Account?.LastLoginProfile;
    public DateTimeOffset? LastLoginIssuedAt => Account?.LastLoginIssuedAt;
    public DateTimeOffset? LastLoginExpiresAt => Account?.LastLoginExpiresAt;
    public EntityManager EntityManager { get; } = new();
    public AvatarSessionState AvatarSession { get; } = new();
    public AvatarEntity? Avatar
    {
        get
        {
            if (AvatarSession.SelectedAvatarEntityId is not Guid avatarEntityId)
            {
                return null;
            }

            return EntityManager.TryGet<AvatarEntity>(avatarEntityId, out AvatarEntity? avatar)
                ? avatar
                : null;
        }
    }

    public uint NextPacketSequence { get; private set; } = 1U;
    public int SentPacketCount { get; private set; }
    public int ReceivedPacketCount { get; private set; }
    public DateTimeOffset? LastSentAt { get; private set; }
    public DateTimeOffset? LastReceivedAt { get; private set; }

    public bool IsConnected => Profile is not null;
    public bool HasAccount => Account is not null;
    public bool HasAvatar => Avatar is not null;
    public bool HasConfirmedAvatar => Avatar is not null && AvatarSession.IsSelectionConfirmed;
    public bool HasCachedLoginGrant => Account?.HasCachedLoginGrant ?? false;

    public void ConfigureRpcSender(IClientEntityRpcSender? rpcSender)
    {
        _rpcSender = rpcSender;
        foreach (ClientEntity entity in EntityManager.Snapshot())
        {
            entity.SetRpcSender(rpcSender);
        }
    }

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
            EntityManager.Clear();
            AvatarSession.Clear();
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

    public AvatarEntity CreateTemporaryAvatarSelection()
    {
        EnsureAccountReady();

        string accountId = Account!.AccountId;
        Guid avatarEntityId = Guid.NewGuid();
        string avatarName = BuildTemporaryAvatarName(avatarEntityId);
        return new AvatarEntity(avatarEntityId, accountId, avatarName);
    }

    public AvatarEntity SelectAvatar()
    {
        AvatarEntity avatar = CreateTemporaryAvatarSelection();
        SelectAvatar(avatar);
        return avatar;
    }

    public void SelectAvatar(AvatarEntity avatar)
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

        if (AvatarSession.SelectedAvatarEntityId.HasValue &&
            AvatarSession.SelectedAvatarEntityId.Value != avatar.EntityId &&
            EntityManager.TryGet<AvatarEntity>(AvatarSession.SelectedAvatarEntityId.Value, out AvatarEntity? previousAvatar) &&
            !AvatarSession.IsSelectionConfirmed)
        {
            previousAvatar.SetRpcSender(null);
            EntityManager.Unregister(previousAvatar.EntityId);
        }

        if (!EntityManager.TryGet<AvatarEntity>(avatar.EntityId, out AvatarEntity? existingAvatar))
        {
            EntityManagerErrorCode registerResult = EntityManager.Register(avatar);
            if (registerResult != EntityManagerErrorCode.None)
            {
                throw new InvalidOperationException(
                    $"Failed to register AvatarEntity {avatar.AvatarId}: {EntityManagerError.Message(registerResult)}");
            }
        }
        else if (!ReferenceEquals(existingAvatar, avatar))
        {
            throw new InvalidOperationException(
                $"Client EntityManager already contains AvatarEntity {avatar.AvatarId}.");
        }

        avatar.SetRpcSender(_rpcSender);
        AvatarSession.Bind(avatar.EntityId);
        RefreshLifecycleState();
    }

    public bool ConfirmAvatarSelection(
        string accountId,
        string avatarId,
        string? avatarName = null,
        string? gameNodeId = null,
        ulong? sessionId = null)
    {
        EnsureAccountReady();

        if (!Guid.TryParse(avatarId, out Guid avatarEntityId))
        {
            return false;
        }

        AvatarEntity? currentAvatar = Avatar;
        if (currentAvatar is null ||
            !string.Equals(Account!.AccountId, accountId, StringComparison.Ordinal) ||
            currentAvatar.EntityId != avatarEntityId)
        {
            return false;
        }

        currentAvatar.UpdateDisplayName(avatarName);
        AvatarSession.Confirm(gameNodeId, sessionId);
        RefreshLifecycleState();
        return true;
    }

    public void ClearAvatarSelection()
    {
        if (Account is null)
        {
            return;
        }

        Guid? avatarEntityId = AvatarSession.SelectedAvatarEntityId;
        AvatarSession.Clear();
        if (avatarEntityId.HasValue)
        {
            if (EntityManager.TryGet<AvatarEntity>(avatarEntityId.Value, out AvatarEntity? avatar))
            {
                avatar.SetRpcSender(null);
            }

            EntityManager.Unregister(avatarEntityId.Value);
        }

        RefreshLifecycleState();
    }

    public bool TryHandleServerRpcPacket(PacketView packet, out string? message)
    {
        if (packet.Header.MsgId != EntityRpcMessageIds.ServerToClientEntityRpcMsgId)
        {
            message = null;
            return false;
        }

        EntityRpcDispatchErrorCode result = ClientEntityRpcDispatcher.Dispatch(
            this,
            packet.Payload,
            out Guid entityId,
            out string rpcName,
            out string errorMessage);
        message = result == EntityRpcDispatchErrorCode.None
            ? $"clientRpc delivered entityId={entityId:D} rpc={rpcName}"
            : $"clientRpc failed entityId={entityId:D} rpc={rpcName} error={errorMessage}";
        return true;
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
                "The simulated client does not have a local Avatar bound to an Account yet. Run login <url> <account> <password> and then selectAvatar first.");
        }

        if (!AvatarSession.IsSelectionConfirmed)
        {
            throw new InvalidOperationException(
                "The local Avatar selection is still waiting for server confirmation. Wait for the selectAvatar success response first.");
        }
    }

    private void RefreshLifecycleState()
    {
        if (Avatar is not null && AvatarSession.IsSelectionConfirmed)
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

    private static string BuildTemporaryAvatarName(Guid avatarEntityId)
    {
        string avatarId = avatarEntityId.ToString("D");
        int suffixLength = Math.Min(8, avatarId.Length);
        return $"TempAvatar-{avatarId[..suffixLength]}";
    }
}

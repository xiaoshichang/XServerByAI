using System.IO;
using XServer.Client.Configuration;
using XServer.Client.Entities;
using XServer.Client.Rpc;
using XServer.Managed.Foundation.Protocol;

namespace XServer.Client.Runtime;

public sealed partial class GameInstance
{
    private IClientEntityRpcSender? _rpcSender;

    public event Action<string>? ClientNetworkMessageGenerated;

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

    private void PublishClientNetworkMessage(string? message)
    {
        if (!string.IsNullOrWhiteSpace(message))
        {
            ClientNetworkMessageGenerated?.Invoke(message);
        }
    }
}

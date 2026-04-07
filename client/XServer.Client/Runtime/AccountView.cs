using XServer.Client.Configuration;

namespace XServer.Client.Runtime;

public sealed class AccountView
{
    public required string AccountId { get; init; }
    public ResolvedClientProfile? LastLoginProfile { get; private set; }
    public DateTimeOffset? LastLoginIssuedAt { get; private set; }
    public DateTimeOffset? LastLoginExpiresAt { get; private set; }
    public AvatarView? Avatar { get; private set; }

    public bool HasCachedLoginGrant => LastLoginProfile is not null;
    public bool HasAvatarBinding => Avatar is not null;

    public void StoreLoginGrant(
        ResolvedClientProfile profile,
        DateTimeOffset issuedAt,
        DateTimeOffset expiresAt)
    {
        LastLoginProfile = profile ?? throw new ArgumentNullException(nameof(profile));
        LastLoginIssuedAt = issuedAt;
        LastLoginExpiresAt = expiresAt;
    }

    public void BindAvatar(AvatarView avatar)
    {
        Avatar = avatar ?? throw new ArgumentNullException(nameof(avatar));
    }
}

using XServer.Client.Configuration;

namespace XServer.Client.Runtime;

public sealed class AccountView
{
    public required string AccountId { get; init; }
    public ResolvedClientProfile? LastLoginProfile { get; private set; }
    public DateTimeOffset? LastLoginIssuedAt { get; private set; }
    public DateTimeOffset? LastLoginExpiresAt { get; private set; }

    public bool HasCachedLoginGrant => LastLoginProfile is not null;

    public void StoreLoginGrant(
        ResolvedClientProfile profile,
        DateTimeOffset issuedAt,
        DateTimeOffset expiresAt)
    {
        LastLoginProfile = profile ?? throw new ArgumentNullException(nameof(profile));
        LastLoginIssuedAt = issuedAt;
        LastLoginExpiresAt = expiresAt;
    }
}

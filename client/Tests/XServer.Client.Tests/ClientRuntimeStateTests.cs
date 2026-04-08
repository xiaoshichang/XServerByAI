using XServer.Client.Configuration;
using XServer.Client.Runtime;

namespace XServer.Client.Tests;

public sealed class ClientRuntimeStateTests
{
    [Fact]
    public void StoreLoginGrantCreatesAccountAndKeepsAvatarSelectionPending()
    {
        ClientRuntimeState state = new();
        ResolvedClientProfile profile = CreateProfile();
        DateTimeOffset issuedAt = DateTimeOffset.FromUnixTimeMilliseconds(1712131200000);
        DateTimeOffset expiresAt = issuedAt.AddMinutes(5);

        state.StoreLoginGrant("demo-account", profile, issuedAt, expiresAt);

        Assert.Equal(ClientLifecycleState.LoggedIn, state.LifecycleState);
        Assert.NotNull(state.Account);
        Assert.Equal("demo-account", state.Account!.AccountId);
        Assert.True(state.HasAccount);
        Assert.False(state.HasAvatar);
        Assert.Equal(profile, state.LastLoginProfile);
        Assert.Equal(issuedAt, state.LastLoginIssuedAt);
        Assert.Equal(expiresAt, state.LastLoginExpiresAt);
        Assert.Contains("avatarSelection=<waiting>", state.BuildStatusText(0, 0U, 0U), StringComparison.Ordinal);

        AvatarView avatar = state.SelectAvatar();

        Assert.Equal(ClientLifecycleState.AvatarSelecting, state.LifecycleState);
        Assert.NotNull(state.Avatar);
        Assert.Same(state.Avatar, state.Account.Avatar);
        Assert.Equal("demo-account", state.Avatar!.AccountId);
        Assert.Same(avatar, state.Avatar);
        Assert.True(Guid.TryParse(state.Avatar.AvatarId, out Guid avatarId));
        Assert.NotEqual(Guid.Empty, avatarId);
        Assert.Equal($"TempAvatar-{state.Avatar.AvatarId[..8]}", state.Avatar.DisplayName);
        Assert.False(state.Avatar.IsServerConfirmed);

        Assert.True(state.ConfirmAvatarSelection("demo-account", state.Avatar.AvatarId, state.Avatar.DisplayName));
        Assert.True(state.HasConfirmedAvatar);
        Assert.True(state.Avatar.IsServerConfirmed);
        Assert.Equal(ClientLifecycleState.AvatarReady, state.LifecycleState);
    }

    [Fact]
    public void DisconnectKeepsAccountAndAvatarBindingButClearsTransport()
    {
        ClientRuntimeState state = new();
        ResolvedClientProfile profile = CreateProfile();

        state.StoreLoginGrant("demo-account", profile, DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));
        state.SelectAvatar();
        Assert.True(state.ConfirmAvatarSelection("demo-account", state.Avatar!.AvatarId, state.Avatar.DisplayName));
        state.MarkConnected(profile, "127.0.0.1:54000");

        state.MarkDisconnected();

        Assert.False(state.IsConnected);
        Assert.Null(state.Profile);
        Assert.Null(state.LocalEndpointText);
        Assert.NotNull(state.Account);
        Assert.NotNull(state.Avatar);
        Assert.Equal(ClientLifecycleState.AvatarReady, state.LifecycleState);
    }

    [Fact]
    public void NewLoginReplacesPreviousAccountAndAvatarBinding()
    {
        ClientRuntimeState state = new();
        ResolvedClientProfile firstProfile = CreateProfile(conversation: 101U);
        ResolvedClientProfile secondProfile = CreateProfile(conversation: 202U, endpointSource: "http login");

        state.StoreLoginGrant("account-a", firstProfile, DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));
        state.SelectAvatar();

        state.StoreLoginGrant("account-b", secondProfile, DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(10));

        Assert.NotNull(state.Account);
        Assert.Equal("account-b", state.Account!.AccountId);
        Assert.Null(state.Avatar);
        Assert.False(state.HasAvatar);
        Assert.Equal(ClientLifecycleState.LoggedIn, state.LifecycleState);
        Assert.Equal(secondProfile, state.LastLoginProfile);
    }

    [Fact]
    public void BuildStatusTextIncludesSeparateAccountAndAvatarSections()
    {
        ClientRuntimeState state = new();
        ResolvedClientProfile profile = CreateProfile();

        state.StoreLoginGrant("demo-account", profile, DateTimeOffset.FromUnixTimeMilliseconds(1712131200000), DateTimeOffset.FromUnixTimeMilliseconds(1712131500000));
        AvatarView avatar = state.SelectAvatar();
        Assert.Equal(ClientLifecycleState.AvatarSelecting, state.LifecycleState);

        Assert.True(state.ConfirmAvatarSelection("demo-account", avatar.AvatarId, avatar.DisplayName));

        string status = state.BuildStatusText(3, 11U, 7U);

        Assert.Contains("Account: id=demo-account", status, StringComparison.Ordinal);
        Assert.Contains($"avatarSelection={avatar.AvatarId}", status, StringComparison.Ordinal);
        Assert.Contains("avatarConfirmed=True", status, StringComparison.Ordinal);
        Assert.Contains(
            $"Avatar: id={avatar.AvatarId}, account=demo-account, name={avatar.DisplayName}, confirmed=True",
            status,
            StringComparison.Ordinal);
    }

    [Fact]
    public void SelectAvatarGeneratesDifferentTemporaryAvatarIdsAcrossSelections()
    {
        ClientRuntimeState state = new();
        ResolvedClientProfile profile = CreateProfile();
        state.StoreLoginGrant("demo-account", profile, DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));

        AvatarView firstAvatar = state.SelectAvatar();
        AvatarView secondSelection = state.CreateTemporaryAvatarSelection();

        Assert.NotEqual(firstAvatar.AvatarId, secondSelection.AvatarId);
        Assert.Equal("demo-account", secondSelection.AccountId);
        Assert.StartsWith("TempAvatar-", secondSelection.DisplayName, StringComparison.Ordinal);
    }

    [Fact]
    public void ConfirmAvatarSelectionRejectsMismatchedPendingAvatar()
    {
        ClientRuntimeState state = new();
        ResolvedClientProfile profile = CreateProfile();
        state.StoreLoginGrant("demo-account", profile, DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));
        AvatarView avatar = state.SelectAvatar();

        Assert.False(state.ConfirmAvatarSelection("other-account", avatar.AvatarId, avatar.DisplayName));
        Assert.False(state.ConfirmAvatarSelection("demo-account", Guid.NewGuid().ToString("D"), avatar.DisplayName));
        Assert.False(state.HasConfirmedAvatar);
        Assert.Equal(ClientLifecycleState.AvatarSelecting, state.LifecycleState);
    }

    [Fact]
    public void ClearAvatarSelectionReturnsToLoggedInState()
    {
        ClientRuntimeState state = new();
        ResolvedClientProfile profile = CreateProfile();
        state.StoreLoginGrant("demo-account", profile, DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));
        state.SelectAvatar();

        state.ClearAvatarSelection();

        Assert.False(state.HasAvatar);
        Assert.False(state.HasConfirmedAvatar);
        Assert.Equal(ClientLifecycleState.LoggedIn, state.LifecycleState);
    }

    private static ResolvedClientProfile CreateProfile(
        uint conversation = 321U,
        string endpointSource = "config")
    {
        return new ResolvedClientProfile(
            "configs/local-dev.json",
            "Gate0",
            "127.0.0.1",
            4000,
            conversation,
            new KcpTransportOptions(1200, 256, 128, true, 10, 2, false, 30, 20, false),
            "127.0.0.1",
            4100,
            endpointSource);
    }
}

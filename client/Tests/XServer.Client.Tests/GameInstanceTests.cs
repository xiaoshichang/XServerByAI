using XServer.Client.Configuration;
using XServer.Client.Entities;
using XServer.Client.Runtime;

namespace XServer.Client.Tests;

public sealed class GameInstanceTests
{
    [Fact]
    public void StoreLoginGrantCreatesAccountAndKeepsAvatarSelectionPending()
    {
        GameInstance gameInstance = new();
        ResolvedClientProfile profile = CreateProfile();
        DateTimeOffset issuedAt = DateTimeOffset.FromUnixTimeMilliseconds(1712131200000);
        DateTimeOffset expiresAt = issuedAt.AddMinutes(5);

        gameInstance.StoreLoginGrant("demo-account", profile, issuedAt, expiresAt);

        Assert.Equal(ClientLifecycleState.LoggedIn, gameInstance.LifecycleState);
        Assert.NotNull(gameInstance.Account);
        Assert.Equal("demo-account", gameInstance.Account!.AccountId);
        Assert.True(gameInstance.HasAccount);
        Assert.False(gameInstance.HasAvatar);
        Assert.Equal(profile, gameInstance.LastLoginProfile);
        Assert.Equal(issuedAt, gameInstance.LastLoginIssuedAt);
        Assert.Equal(expiresAt, gameInstance.LastLoginExpiresAt);
        Assert.Equal(0, gameInstance.EntityManager.Count);
        Assert.Contains("AvatarSession: <none>", gameInstance.BuildStatusText(0, 0U, 0U), StringComparison.Ordinal);

        AvatarEntity avatar = gameInstance.SelectAvatar();

        Assert.Equal(ClientLifecycleState.AvatarSelecting, gameInstance.LifecycleState);
        Assert.NotNull(gameInstance.Avatar);
        Assert.Equal(gameInstance.Avatar!.EntityId, gameInstance.AvatarSession.SelectedAvatarEntityId);
        Assert.Equal("demo-account", gameInstance.Avatar!.AccountId);
        Assert.Same(avatar, gameInstance.Avatar);
        Assert.Equal(1, gameInstance.EntityManager.Count);
        Assert.True(Guid.TryParse(gameInstance.Avatar.AvatarId, out Guid avatarId));
        Assert.NotEqual(Guid.Empty, avatarId);
        Assert.False(gameInstance.HasConfirmedAvatar);
        Assert.False(gameInstance.AvatarSession.IsSelectionConfirmed);

        Assert.True(gameInstance.ConfirmAvatarSelection("demo-account", gameInstance.Avatar.AvatarId, "Game0", 7UL));
        Assert.True(gameInstance.HasConfirmedAvatar);
        Assert.True(gameInstance.AvatarSession.IsSelectionConfirmed);
        Assert.Equal("Game0", gameInstance.AvatarSession.GameNodeId);
        Assert.Equal(7UL, gameInstance.AvatarSession.SessionId);
        Assert.Equal(ClientLifecycleState.AvatarReady, gameInstance.LifecycleState);
    }

    [Fact]
    public void DisconnectKeepsAccountAndAvatarBindingButClearsTransport()
    {
        GameInstance gameInstance = new();
        ResolvedClientProfile profile = CreateProfile();

        gameInstance.StoreLoginGrant("demo-account", profile, DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));
        gameInstance.SelectAvatar();
        Assert.True(gameInstance.ConfirmAvatarSelection("demo-account", gameInstance.Avatar!.AvatarId));
        gameInstance.MarkConnected(profile, "127.0.0.1:54000");

        gameInstance.MarkDisconnected();

        Assert.False(gameInstance.IsConnected);
        Assert.Null(gameInstance.Profile);
        Assert.Null(gameInstance.LocalEndpointText);
        Assert.NotNull(gameInstance.Account);
        Assert.NotNull(gameInstance.Avatar);
        Assert.Equal(ClientLifecycleState.AvatarReady, gameInstance.LifecycleState);
    }

    [Fact]
    public void NewLoginReplacesPreviousAccountAndAvatarBinding()
    {
        GameInstance gameInstance = new();
        ResolvedClientProfile firstProfile = CreateProfile(conversation: 101U);
        ResolvedClientProfile secondProfile = CreateProfile(conversation: 202U, endpointSource: "http login");

        gameInstance.StoreLoginGrant("account-a", firstProfile, DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));
        gameInstance.SelectAvatar();

        gameInstance.StoreLoginGrant("account-b", secondProfile, DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(10));

        Assert.NotNull(gameInstance.Account);
        Assert.Equal("account-b", gameInstance.Account!.AccountId);
        Assert.Null(gameInstance.Avatar);
        Assert.False(gameInstance.HasAvatar);
        Assert.Equal(0, gameInstance.EntityManager.Count);
        Assert.Equal(ClientLifecycleState.LoggedIn, gameInstance.LifecycleState);
        Assert.Equal(secondProfile, gameInstance.LastLoginProfile);
    }

    [Fact]
    public void BuildStatusTextIncludesSeparateAccountAndAvatarSections()
    {
        GameInstance gameInstance = new();
        ResolvedClientProfile profile = CreateProfile();

        gameInstance.StoreLoginGrant("demo-account", profile, DateTimeOffset.FromUnixTimeMilliseconds(1712131200000), DateTimeOffset.FromUnixTimeMilliseconds(1712131500000));
        AvatarEntity avatar = gameInstance.SelectAvatar();
        Assert.Equal(ClientLifecycleState.AvatarSelecting, gameInstance.LifecycleState);

        Assert.True(gameInstance.ConfirmAvatarSelection("demo-account", avatar.AvatarId));

        string status = gameInstance.BuildStatusText(3, 11U, 7U);

        Assert.Contains("Entities: total=1", status, StringComparison.Ordinal);
        Assert.Contains("Account: id=demo-account", status, StringComparison.Ordinal);
        Assert.Contains($"AvatarSession: entityId={avatar.EntityId:D}, confirmed=True", status, StringComparison.Ordinal);
        Assert.Contains(
            $"Avatar: id={avatar.AvatarId}, account=demo-account",
            status,
            StringComparison.Ordinal);
    }

    [Fact]
    public void SelectAvatarGeneratesDifferentTemporaryAvatarIdsAcrossSelections()
    {
        GameInstance gameInstance = new();
        ResolvedClientProfile profile = CreateProfile();
        gameInstance.StoreLoginGrant("demo-account", profile, DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));

        AvatarEntity firstAvatar = gameInstance.SelectAvatar();
        AvatarEntity secondSelection = gameInstance.CreateTemporaryAvatarSelection();

        Assert.NotEqual(firstAvatar.AvatarId, secondSelection.AvatarId);
        Assert.Equal("demo-account", secondSelection.AccountId);
    }

    [Fact]
    public void ConfirmAvatarSelectionRejectsMismatchedPendingAvatar()
    {
        GameInstance gameInstance = new();
        ResolvedClientProfile profile = CreateProfile();
        gameInstance.StoreLoginGrant("demo-account", profile, DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));
        AvatarEntity avatar = gameInstance.SelectAvatar();

        Assert.False(gameInstance.ConfirmAvatarSelection("other-account", avatar.AvatarId));
        Assert.False(gameInstance.ConfirmAvatarSelection("demo-account", Guid.NewGuid().ToString("D")));
        Assert.False(gameInstance.HasConfirmedAvatar);
        Assert.Equal(ClientLifecycleState.AvatarSelecting, gameInstance.LifecycleState);
    }

    [Fact]
    public void ClearAvatarSelectionReturnsToLoggedInState()
    {
        GameInstance gameInstance = new();
        ResolvedClientProfile profile = CreateProfile();
        gameInstance.StoreLoginGrant("demo-account", profile, DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));
        gameInstance.SelectAvatar();

        gameInstance.ClearAvatarSelection();

        Assert.False(gameInstance.HasAvatar);
        Assert.False(gameInstance.HasConfirmedAvatar);
        Assert.Equal(0, gameInstance.EntityManager.Count);
        Assert.Equal(ClientLifecycleState.LoggedIn, gameInstance.LifecycleState);
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

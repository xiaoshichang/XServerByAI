using XServer.Client.Configuration;
using XServer.Client.Runtime;

namespace XServer.Client.Tests;

public sealed class ClientRuntimeStateTests
{
    [Fact]
    public void StoreLoginGrantCreatesAccountAndBindsAvatarSeparately()
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

        state.MarkLocalAvatarReady("avatar:hero", "Hero");

        Assert.Equal(ClientLifecycleState.AvatarReady, state.LifecycleState);
        Assert.NotNull(state.Avatar);
        Assert.Same(state.Avatar, state.Account.Avatar);
        Assert.Equal("demo-account", state.Avatar!.AccountId);
        Assert.Equal("avatar:hero", state.Avatar.AvatarId);
        Assert.Equal("Hero", state.Avatar.DisplayName);
    }

    [Fact]
    public void DisconnectKeepsAccountAndAvatarBindingButClearsTransport()
    {
        ClientRuntimeState state = new();
        ResolvedClientProfile profile = CreateProfile();

        state.StoreLoginGrant("demo-account", profile, DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5));
        state.MarkLocalAvatarReady("avatar:demo-account");
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
        state.MarkLocalAvatarReady("avatar:account-a");

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
        state.MarkLocalAvatarReady("avatar:demo-account", "Hero");

        string status = state.BuildStatusText(3, 11U, 7U);

        Assert.Contains("Account: id=demo-account", status, StringComparison.Ordinal);
        Assert.Contains("avatar=avatar:demo-account", status, StringComparison.Ordinal);
        Assert.Contains("Avatar: id=avatar:demo-account, account=demo-account, name=Hero", status, StringComparison.Ordinal);
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

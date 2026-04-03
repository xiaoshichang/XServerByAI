namespace XServer.Client.Runtime;

public enum ClientLifecycleState
{
    Disconnected = 0,
    Connected = 1,
    LoginPending = 2,
    LoggedIn = 3,
    AvatarReady = 4,
}

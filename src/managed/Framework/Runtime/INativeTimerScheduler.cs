namespace XServer.Managed.Framework.Runtime
{
    public interface INativeTimerScheduler
    {
        long CreateOnce(TimeSpan delay, Action callback);

        bool Cancel(long timerId);
    }
}

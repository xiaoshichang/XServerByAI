namespace XServer.Managed.Framework.Runtime
{
    public enum NativeTimerErrorCode : long
    {
        None = 0,
        InvalidTimerId = -1,
        TimerNotFound = -2,
        CallbackEmpty = -3,
        IntervalMustBePositive = -4,
        TimerIdExhausted = -5,
        Unknown = -6,
    }

    public static class NativeTimerResult
    {
        public static bool IsTimerId(long value)
        {
            return value > 0;
        }
    }
}

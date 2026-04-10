namespace XServer.Managed.Framework.Runtime
{
    public enum GameNodeRuntimeStateErrorCode
    {
        None = 0,
        InvalidArgument = 1,
        UnknownStubType = 2,
        StubInstantiationFailed = 3,
        DuplicateEntityId = 4,
    }

    public static class GameNodeRuntimeStateError
    {
        public static string Message(GameNodeRuntimeStateErrorCode code)
        {
            return code switch
            {
                GameNodeRuntimeStateErrorCode.None => "No error.",
                GameNodeRuntimeStateErrorCode.InvalidArgument => "Game node runtime state argument is invalid.",
                GameNodeRuntimeStateErrorCode.UnknownStubType =>
                    "Game node runtime state could not resolve the requested stub.",
                GameNodeRuntimeStateErrorCode.StubInstantiationFailed =>
                    "Game node runtime state failed to instantiate the requested stub.",
                GameNodeRuntimeStateErrorCode.DuplicateEntityId =>
                    "Game node runtime state encountered a duplicate entity ID.",
                _ => "Unknown game node runtime state error.",
            };
        }
    }

    public enum MailboxCallErrorCode
    {
        None = 0,
        InvalidArgument = 1,
        InvalidMessageId = 2,
        UnknownTargetMailbox = 3,
        TargetNodeUnavailable = 4,
        MailboxRejected = 5,
    }

    public static class MailboxCallError
    {
        public static string Message(MailboxCallErrorCode code)
        {
            return code switch
            {
                MailboxCallErrorCode.None => "No error.",
                MailboxCallErrorCode.InvalidArgument => "Mailbox call argument is invalid.",
                MailboxCallErrorCode.InvalidMessageId => "Mailbox call msgId must not be zero.",
                MailboxCallErrorCode.UnknownTargetMailbox => "Mailbox target is unknown or has no owner.",
                MailboxCallErrorCode.TargetNodeUnavailable => "Mailbox target node is unavailable.",
                MailboxCallErrorCode.MailboxRejected => "Target mailbox rejected the incoming call.",
                _ => "Unknown mailbox call error.",
            };
        }
    }

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

    public enum ProxyCallErrorCode
    {
        None = 0,
        InvalidArgument = 1,
        InvalidMessageId = 2,
        UnknownTargetEntity = 3,
        TargetNodeUnavailable = 4,
        EntityRejected = 5,
    }

    public static class ProxyCallError
    {
        public static string Message(ProxyCallErrorCode code)
        {
            return code switch
            {
                ProxyCallErrorCode.None => "No error.",
                ProxyCallErrorCode.InvalidArgument => "Proxy call argument is invalid.",
                ProxyCallErrorCode.InvalidMessageId => "Proxy call msgId must not be zero.",
                ProxyCallErrorCode.UnknownTargetEntity => "Proxy call target entity is unknown or offline.",
                ProxyCallErrorCode.TargetNodeUnavailable => "Proxy call route gate is unavailable.",
                ProxyCallErrorCode.EntityRejected => "Target entity rejected the incoming proxy call.",
                _ => "Unknown proxy call error.",
            };
        }
    }

    public enum StubCallErrorCode
    {
        None = 0,
        InvalidArgument = 1,
        InvalidMessageId = 2,
        UnknownTargetStub = 3,
        TargetNodeUnavailable = 4,
        StubRejected = 5,
    }

    public static class StubCallError
    {
        public static string Message(StubCallErrorCode code)
        {
            return code switch
            {
                StubCallErrorCode.None => "No error.",
                StubCallErrorCode.InvalidArgument => "Stub call argument is invalid.",
                StubCallErrorCode.InvalidMessageId => "Stub call msgId must not be zero.",
                StubCallErrorCode.UnknownTargetStub => "Stub call target type is unknown or has no owner.",
                StubCallErrorCode.TargetNodeUnavailable => "Stub call target node is unavailable.",
                StubCallErrorCode.StubRejected => "Target stub rejected the incoming call.",
                _ => "Unknown stub call error.",
            };
        }
    }
}

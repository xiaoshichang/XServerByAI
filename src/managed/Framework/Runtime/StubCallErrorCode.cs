namespace XServer.Managed.Framework.Runtime
{
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

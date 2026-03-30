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
}
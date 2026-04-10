namespace XServer.Managed.Framework.Runtime
{
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
}

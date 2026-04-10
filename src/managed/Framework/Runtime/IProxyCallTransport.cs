using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Runtime
{
    public interface IProxyCallTransport
    {
        ProxyCallErrorCode Forward(
            ProxyAddress targetAddress,
            ProxyCallMessage message);
    }
}

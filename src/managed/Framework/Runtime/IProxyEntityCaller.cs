using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Runtime
{
    internal interface IProxyEntityCaller
    {
        void CallProxy(ServerEntity sourceEntity, ProxyAddress targetAddress, ProxyCallMessage message);
    }
}

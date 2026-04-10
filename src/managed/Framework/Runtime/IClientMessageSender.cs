using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Runtime
{
    public interface IClientMessageSender
    {
        void PushToClient(ServerEntity sourceEntity, ProxyAddress targetAddress, ProxyCallMessage message);
    }
}

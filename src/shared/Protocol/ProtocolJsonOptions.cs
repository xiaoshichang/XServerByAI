using System.Text.Json;

#if XSERVER_CLIENT_FRAMEWORK
namespace XServer.Client.Protocol;
#elif XSERVER_SERVER_FRAMEWORK
namespace XServer.Managed.Framework.Protocol;
#else
#error Shared protocol sources must be compiled by a framework project.
#endif

public static class ProtocolJsonOptions
{
    public static JsonSerializerOptions Default { get; } = new()
    {
        PropertyNameCaseInsensitive = true,
    };
}

using System.Text.Json;

#if XSERVER_CLIENT_FRAMEWORK
namespace XServer.Client.Rpc;
#elif XSERVER_MANAGED_FRAMEWORK
namespace XServer.Managed.Framework.Rpc;
#else
#error EntityRpc shared sources must be compiled by a framework project.
#endif

public readonly record struct EntityRpcInvocationEnvelope(
    Guid EntityId,
    string RpcName,
    IReadOnlyList<JsonElement> Arguments);

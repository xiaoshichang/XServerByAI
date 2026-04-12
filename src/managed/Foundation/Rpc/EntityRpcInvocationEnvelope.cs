using System.Text.Json;

namespace XServer.Managed.Foundation.Rpc;

public readonly record struct EntityRpcInvocationEnvelope(
    Guid EntityId,
    string RpcName,
    IReadOnlyList<JsonElement> Arguments);

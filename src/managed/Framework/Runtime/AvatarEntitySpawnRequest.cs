using System;

namespace XServer.Managed.Framework.Runtime
{
    public readonly record struct AvatarEntitySpawnRequest(
        Guid EntityId,
        string AccountId,
        string AvatarName,
        string RouteGateNodeId,
        ulong SessionId);
}

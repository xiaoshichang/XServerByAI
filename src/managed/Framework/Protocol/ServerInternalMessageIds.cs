namespace XServer.Managed.Framework.Protocol;

public static class ServerInternalMessageIds
{
    public const uint msgid_gate_gm_register = 1000U;
    public const uint msgid_game_gm_register = msgid_gate_gm_register;
    public const uint msgid_game_gate_register = msgid_gate_gm_register;

    public const uint msgid_gate_gm_heartbeat = 1100U;
    public const uint msgid_game_gm_heartbeat = msgid_gate_gm_heartbeat;
    public const uint msgid_game_gate_heartbeat = msgid_gate_gm_heartbeat;

    public const uint msgid_gm_gate_clusterready = 1201U;
    public const uint msgid_gm_game_serverstubownershipsync = 1202U;
    public const uint msgid_game_gm_gameservicereadyreport = 1203U;
    public const uint msgid_gm_game_clusternodesonlinenotify = 1204U;
    public const uint msgid_game_gm_gamegatemeshreadyreport = 1205U;

    public const uint msgid_gate_game_forwardtogame = 2000U;
    public const uint msgid_game_gate_pushtoclient = 2001U;
    public const uint msgid_game_game_forwardmailboxcall = 2002U;
    public const uint msgid_gate_game_createavatarentity = 2003U;
    public const uint msgid_game_gate_avatarentitycreateresult = 2004U;
    public const uint msgid_game_game_forwardproxycall = 2005U;

    public const uint Register = msgid_gate_gm_register;
    public const uint Heartbeat = msgid_gate_gm_heartbeat;
    public const uint ClusterReadyNotify = msgid_gm_gate_clusterready;
    public const uint ServerStubOwnershipSync = msgid_gm_game_serverstubownershipsync;
    public const uint GameServiceReadyReport = msgid_game_gm_gameservicereadyreport;
    public const uint ClusterNodesOnlineNotify = msgid_gm_game_clusternodesonlinenotify;
    public const uint GameGateMeshReadyReport = msgid_game_gm_gamegatemeshreadyreport;
    public const uint ForwardToGame = msgid_gate_game_forwardtogame;
    public const uint PushToClient = msgid_game_gate_pushtoclient;
    public const uint ForwardMailboxCall = msgid_game_game_forwardmailboxcall;
    public const uint CreateAvatarEntity = msgid_gate_game_createavatarentity;
    public const uint AvatarEntityCreateResult = msgid_game_gate_avatarentitycreateresult;
    public const uint ForwardProxyCall = msgid_game_game_forwardproxycall;
}

using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using XServer.Managed.Framework.Entities;
using XServer.Managed.Framework.Interop;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Tests
{
    public unsafe class GameNativeExportsTests
    {
        [Fact]
        public void GameNativeOnMessage_CreateAvatarEntity_AcceptsCamelCasePayload()
        {
            InitializeRuntime("GameInteropTest");
            AssignOnlineStubOwnership("GameInteropTest");

            Guid avatarEntityId = Guid.NewGuid();
            SendCreateAvatarMessage(avatarEntityId, "demo-account", "TempHero", "Gate0", 42);

            GameNodeRuntimeState runtimeState = GetRuntimeState();
            OnlineStub onlineStub = Assert.IsType<OnlineStub>(runtimeState.OwnedServerStubs.Single());
            Assert.True(onlineStub.TryGetRegisteredAvatar(avatarEntityId, out OnlineAvatarRegistration registration));
            Assert.Equal("demo-account", registration.AccountId);
            Assert.Equal((ulong)42, registration.SessionId);
            Assert.Equal("Gate0", registration.GateNodeId);
            Assert.Equal("GameInteropTest", registration.GameNodeId);
            Assert.Equal(avatarEntityId, registration.Proxy.EntityId);
            Assert.Equal("Gate0", registration.Proxy.RouteGateNodeId);
        }

        [Fact]
        public void GameNativeOnMessage_ForwardProxyCall_DeliversMessageToAvatarEntity()
        {
            InitializeRuntime("GameProxyInteropTest");
            AssignOnlineStubOwnership("GameProxyInteropTest");

            Guid avatarEntityId = Guid.NewGuid();
            SendCreateAvatarMessage(avatarEntityId, "proxy-account", "ProxyHero", "Gate3", 84);

            byte[] forwardedPayload = [0xAB, 0xCD, 0xEF];
            byte[] relayPayload = RelayProxyCallCodec.Encode(
                "GameRemoteSource",
                "Gate3",
                avatarEntityId,
                OnlineStub.BroadcastOnlineAvatarProxyMsgId,
                forwardedPayload);

            fixed (byte* relayPayloadPtr = relayPayload)
            {
                ManagedMessageView message = new()
                {
                    StructSize = (uint)sizeof(ManagedMessageView),
                    MsgId = RelayProxyCallCodec.ForwardProxyCallMsgId,
                    Payload = relayPayloadPtr,
                    PayloadLength = (uint)relayPayload.Length,
                };

                delegate* unmanaged[Cdecl]<ManagedMessageView*, int> onMessage = &GameNativeExports.GameNativeOnMessage;
                int result = onMessage(&message);
                Assert.Equal(0, result);
            }

            GameNodeRuntimeState runtimeState = GetRuntimeState();
            AvatarEntity avatar = Assert.Single(runtimeState.AvatarEntities);
            Assert.Collection(
                avatar.ReceivedProxyMessages,
                received =>
                {
                    Assert.Equal(OnlineStub.BroadcastOnlineAvatarProxyMsgId, received.MsgId);
                    Assert.Equal(forwardedPayload, received.Payload);
                });
        }

        private static void InitializeRuntime(string nodeId)
        {
            byte[] nodeIdUtf8 = Encoding.UTF8.GetBytes(nodeId);
            byte[] configPathUtf8 = Encoding.UTF8.GetBytes("configs/local-dev.json");

            fixed (byte* nodeIdPtr = nodeIdUtf8)
            fixed (byte* configPathPtr = configPathUtf8)
            {
                ManagedInitArgs initArgs = new()
                {
                    StructSize = (uint)sizeof(ManagedInitArgs),
                    AbiVersion = ManagedAbi.Version,
                    NodeIdUtf8 = nodeIdPtr,
                    NodeIdLength = (uint)nodeIdUtf8.Length,
                    ConfigPathUtf8 = configPathPtr,
                    ConfigPathLength = (uint)configPathUtf8.Length,
                    NativeCallbacks = new ManagedNativeCallbacks
                    {
                        StructSize = (uint)sizeof(ManagedNativeCallbacks),
                    },
                };

                delegate* unmanaged[Cdecl]<ManagedInitArgs*, int> init = &GameNativeExports.GameNativeInit;
                int initResult = init(&initArgs);
                Assert.Equal(0, initResult);
            }
        }

        private static void AssignOnlineStubOwnership(string ownerGameNodeId)
        {
            GameNodeRuntimeState runtimeState = GetRuntimeState();
            ServerStubOwnershipSnapshot snapshot = new(
                1,
                [
                    new ServerStubOwnershipAssignment(nameof(OnlineStub), "unknown", ownerGameNodeId),
                ]);
            Assert.Equal(GameNodeRuntimeStateErrorCode.None, runtimeState.ApplyOwnership(snapshot));
        }

        private static GameNodeRuntimeState GetRuntimeState()
        {
            FieldInfo? runtimeField = typeof(GameNativeExports).GetField("s_runtimeState", BindingFlags.Static | BindingFlags.NonPublic);
            Assert.NotNull(runtimeField);
            return Assert.IsType<GameNodeRuntimeState>(runtimeField!.GetValue(null));
        }

        private static void SendCreateAvatarMessage(
            Guid avatarEntityId,
            string accountId,
            string avatarName,
            string gateNodeId,
            ulong sessionId)
        {
            string payloadText =
                $$"""{"accountId":"{{accountId}}","avatarId":"{{avatarEntityId:D}}","avatarName":"{{avatarName}}","gateNodeId":"{{gateNodeId}}","sessionId":{{sessionId}}}""";
            byte[] payloadUtf8 = Encoding.UTF8.GetBytes(payloadText);

            fixed (byte* payloadPtr = payloadUtf8)
            {
                ManagedMessageView message = new()
                {
                    StructSize = (uint)sizeof(ManagedMessageView),
                    MsgId = 2003U,
                    Seq = 0U,
                    Flags = 0U,
                    SessionId = 0UL,
                    PlayerId = 0UL,
                    Payload = payloadPtr,
                    PayloadLength = (uint)payloadUtf8.Length,
                };

                delegate* unmanaged[Cdecl]<ManagedMessageView*, int> onMessage = &GameNativeExports.GameNativeOnMessage;
                int result = onMessage(&message);
                Assert.Equal(0, result);
            }
        }
    }
}

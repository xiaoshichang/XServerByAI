using System.Runtime.CompilerServices;
using System.Text;
using XServer.Managed.Framework.Entities;
using XServer.Managed.Framework.Interop;

namespace XServer.Managed.Framework.Tests
{
    public unsafe class GameNativeExportsTests
    {
        [Fact]
        public void GameNativeOnMessage_CreateAvatarEntity_AcceptsCamelCasePayload()
        {
            OnlineStub.ClearRegisteredAvatars();
            try
            {
                byte[] nodeIdUtf8 = Encoding.UTF8.GetBytes("GameInteropTest");
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

                string avatarId = Guid.NewGuid().ToString("D");
                string payloadText =
                    $$"""{"accountId":"demo-account","avatarId":"{{avatarId}}","avatarName":"TempHero","gateNodeId":"Gate0","sessionId":42}""";
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

                Assert.True(OnlineStub.TryGetRegisteredAvatar(Guid.Parse(avatarId), out OnlineAvatarRegistration registration));
                Assert.Equal("demo-account", registration.AccountId);
                Assert.Equal((ulong)42, registration.SessionId);
                Assert.Equal("Gate0", registration.GateNodeId);
                Assert.Equal("GameInteropTest", registration.GameNodeId);
            }
            finally
            {
                OnlineStub.ClearRegisteredAvatars();
            }
        }
    }
}

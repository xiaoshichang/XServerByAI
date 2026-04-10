using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using XServer.Managed.Framework.Catalog;
using XServer.Managed.Framework.Entities;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Interop
{
    public static unsafe class GameNativeExports
    {
        private const int InvalidArgument = -1;
        private const int RuntimeNotInitialized = -2;
        private const int IndexOutOfRange = -3;
        private const int BufferTooSmall = -4;
        private const int RuntimeOperationFailed = -5;
        private const int OwnershipApplyErrorOffset = 1000;
        private const uint CreateAvatarEntityMsgId = 2003u;
        private const string RuntimeLogCategory = "managed.runtime";
        private static readonly JsonSerializerOptions ControlJsonOptions = new()
        {
            PropertyNameCaseInsensitive = true,
        };
        private static ManagedNativeCallbacks s_nativeCallbacks;
        private static ManagedNativeStubCallTransport? s_nativeStubCallTransport;
        private static ManagedNativeProxyCallTransport? s_nativeProxyCallTransport;
        private static ManagedNativeClientMessageTransport? s_nativeClientMessageTransport;
        private static ManagedNativeTimerScheduler? s_nativeTimerScheduler;
        private static GameNodeRuntimeState? s_runtimeState;

        [UnmanagedCallersOnly(EntryPoint = "GameNativeGetAbiVersion", CallConvs = [typeof(CallConvCdecl)])]
        public static uint GameNativeGetAbiVersion()
        {
            return ManagedAbi.Version;
        }

        [UnmanagedCallersOnly(EntryPoint = "GameNativeInit", CallConvs = [typeof(CallConvCdecl)])]
        public static int GameNativeInit(ManagedInitArgs* args)
        {
            if (args == null || args->StructSize < sizeof(ManagedInitArgs) || args->AbiVersion != ManagedAbi.Version)
            {
                return InvalidArgument;
            }

            try
            {
                s_nativeCallbacks = args->NativeCallbacks;
                NativeLoggerBridge.Configure(s_nativeCallbacks);
                s_nativeStubCallTransport = ManagedNativeStubCallTransport.CreateOrNull(s_nativeCallbacks);
                s_nativeProxyCallTransport = ManagedNativeProxyCallTransport.CreateOrNull(s_nativeCallbacks);
                s_nativeClientMessageTransport = ManagedNativeClientMessageTransport.CreateOrNull(s_nativeCallbacks);
                s_nativeTimerScheduler = new ManagedNativeTimerScheduler(s_nativeCallbacks);
                string nodeId = ReadUtf8(args->NodeIdUtf8, args->NodeIdLength);
                _ = ReadUtf8(args->ConfigPathUtf8, args->ConfigPathLength);
                s_runtimeState = new GameNodeRuntimeState(
                    nodeId,
                    NotifyNativeServerStubReady,
                    s_nativeStubCallTransport,
                    s_nativeProxyCallTransport,
                    s_nativeClientMessageTransport,
                    nativeTimerScheduler: s_nativeTimerScheduler);
                NativeLoggerBridge.Info(RuntimeLogCategory, "Game managed runtime initialized.");
                return 0;
            }
            catch
            {
                s_nativeStubCallTransport = null;
                s_nativeProxyCallTransport = null;
                s_nativeClientMessageTransport = null;
                s_nativeTimerScheduler?.Reset();
                s_nativeTimerScheduler = null;
                NativeLoggerBridge.Reset();
                s_nativeCallbacks = default;
                s_runtimeState = null;
                return InvalidArgument;
            }
        }

        [UnmanagedCallersOnly(EntryPoint = "GameNativeOnMessage", CallConvs = [typeof(CallConvCdecl)])]
        public static int GameNativeOnMessage(ManagedMessageView* message)
        {
            if (message == null)
            {
                return 0;
            }

            if (s_runtimeState == null)
            {
                return RuntimeNotInitialized;
            }

            try
            {
                if (message->MsgId == CreateAvatarEntityMsgId)
                {
                    AvatarEntitySpawnRequest createRequest = BuildAvatarEntitySpawnRequest(
                        message->Payload,
                        message->PayloadLength);
                    if (!s_runtimeState.TryCreateAvatarEntity(createRequest, out AvatarEntity? avatar, out string? error))
                    {
                        NativeLoggerBridge.Warn(
                            RuntimeLogCategory,
                            $"Game managed runtime rejected create-avatar request: {error ?? "unknown error"}");
                        return RuntimeOperationFailed;
                    }

                    NativeLoggerBridge.Info(
                        RuntimeLogCategory,
                        $"Game managed runtime created AvatarEntity entityId={avatar!.EntityId} gate={createRequest.RouteGateNodeId}.");
                    return 0;
                }

                if (message->MsgId == RelayProxyCallCodec.ForwardProxyCallMsgId)
                {
                    if (!RelayProxyCallCodec.TryDecode(message->Payload, message->PayloadLength, out RelayProxyCallCodec.RelayProxyCallEnvelope proxyRelay))
                    {
                        NativeLoggerBridge.Warn(RuntimeLogCategory, "Game managed runtime failed to decode forwarded proxy call payload.");
                        return InvalidArgument;
                    }

                    ProxyCallErrorCode proxyResult = s_runtimeState.ReceiveProxyCall(
                        proxyRelay.TargetEntityId,
                        new ProxyCallMessage(proxyRelay.ProxyCallMsgId, proxyRelay.Payload));
                    if (proxyResult != ProxyCallErrorCode.None)
                    {
                        NativeLoggerBridge.Warn(
                            RuntimeLogCategory,
                            $"Game managed runtime rejected forwarded proxy call: {ProxyCallError.Message(proxyResult)}");
                        return (int)proxyResult;
                    }

                    return 0;
                }

                if (message->MsgId != RelayStubCallCodec.ForwardStubCallMsgId)
                {
                    return 0;
                }

                if (!RelayStubCallCodec.TryDecode(message->Payload, message->PayloadLength, out RelayStubCallCodec.RelayStubCallEnvelope relay))
                {
                    NativeLoggerBridge.Warn(RuntimeLogCategory, "Game managed runtime failed to decode forwarded stub call payload.");
                    return InvalidArgument;
                }

                if (!string.Equals(relay.TargetGameNodeId, s_runtimeState.NodeId, StringComparison.Ordinal))
                {
                    NativeLoggerBridge.Warn(RuntimeLogCategory, "Game managed runtime rejected forwarded stub call for another game node.");
                    return InvalidArgument;
                }

                StubCallErrorCode result = s_runtimeState.ReceiveStubCall(
                    relay.TargetStubType,
                    new StubCallMessage(relay.StubCallMsgId, relay.Payload));
                if (result != StubCallErrorCode.None)
                {
                    NativeLoggerBridge.Warn(
                        RuntimeLogCategory,
                        $"Game managed runtime rejected forwarded stub call: {StubCallError.Message(result)}");
                    return (int)result;
                }

                return 0;
            }
            catch (Exception exception)
            {
                NativeLoggerBridge.Warn(
                    RuntimeLogCategory,
                    $"Game managed runtime failed while handling an incoming native message: {exception.GetType().Name}: {exception.Message}");
                return InvalidArgument;
            }
        }

        [UnmanagedCallersOnly(EntryPoint = "GameNativeOnTick", CallConvs = [typeof(CallConvCdecl)])]
        public static int GameNativeOnTick(ulong nowUnixMsUtc, uint deltaMs)
        {
            _ = nowUnixMsUtc;
            _ = deltaMs;
            return 0;
        }

        [UnmanagedCallersOnly(EntryPoint = "GameNativeOnNativeTimer", CallConvs = [typeof(CallConvCdecl)])]
        public static int GameNativeOnNativeTimer(long timerId)
        {
            if (s_nativeTimerScheduler == null)
            {
                return RuntimeNotInitialized;
            }

            return s_nativeTimerScheduler.HandleTimerFired(timerId);
        }

        [UnmanagedCallersOnly(EntryPoint = "GameNativeApplyServerStubOwnership", CallConvs = [typeof(CallConvCdecl)])]
        public static int GameNativeApplyServerStubOwnership(ManagedServerStubOwnershipSync* sync)
        {
            if (s_runtimeState == null)
            {
                return RuntimeNotInitialized;
            }

            try
            {
                ServerStubOwnershipSnapshot snapshot = BuildOwnershipSnapshot(sync);
                GameNodeRuntimeStateErrorCode result = s_runtimeState.ApplyOwnership(snapshot);
                if (result == GameNodeRuntimeStateErrorCode.None)
                {
                    NativeLoggerBridge.Info(RuntimeLogCategory, "Game managed runtime applied server stub ownership.");
                    return 0;
                }

                NativeLoggerBridge.Warn(RuntimeLogCategory, "Game managed runtime rejected server stub ownership.");
                return OwnershipApplyErrorOffset + (int)result;
            }
            catch
            {
                NativeLoggerBridge.Warn(RuntimeLogCategory, "Game managed runtime failed to decode server stub ownership.");
                return InvalidArgument;
            }
        }

        [UnmanagedCallersOnly(EntryPoint = "GameNativeResetServerStubOwnership", CallConvs = [typeof(CallConvCdecl)])]
        public static int GameNativeResetServerStubOwnership()
        {
            if (s_runtimeState == null)
            {
                return RuntimeNotInitialized;
            }

            s_runtimeState.ResetOwnership();
            NativeLoggerBridge.Info(RuntimeLogCategory, "Game managed runtime reset server stub ownership state.");
            return 0;
        }

        [UnmanagedCallersOnly(EntryPoint = "GameNativeGetReadyServerStubCount", CallConvs = [typeof(CallConvCdecl)])]
        public static int GameNativeGetReadyServerStubCount(uint* count)
        {
            if (count == null)
            {
                return InvalidArgument;
            }

            if (s_runtimeState == null)
            {
                *count = 0;
                return RuntimeNotInitialized;
            }

            try
            {
                *count = checked((uint)GetReadyServerStubs().Count);
                return 0;
            }
            catch
            {
                *count = 0;
                return InvalidArgument;
            }
        }

        [UnmanagedCallersOnly(EntryPoint = "GameNativeGetReadyServerStubEntry", CallConvs = [typeof(CallConvCdecl)])]
        public static int GameNativeGetReadyServerStubEntry(uint index, ManagedServerStubReadyEntry* entry)
        {
            if (entry == null || entry->StructSize < sizeof(ManagedServerStubReadyEntry))
            {
                return InvalidArgument;
            }

            ResetReadyEntry(entry);
            if (s_runtimeState == null)
            {
                return RuntimeNotInitialized;
            }

            try
            {
                IReadOnlyList<ServerStubEntity> readyStubs = GetReadyServerStubs();
                if (index >= (uint)readyStubs.Count)
                {
                    return IndexOutOfRange;
                }

                ServerStubEntity stub = readyStubs[(int)index];
                return WriteReadyEntry(stub, entry);
            }
            catch
            {
                ResetReadyEntry(entry);
                return InvalidArgument;
            }
        }

        [UnmanagedCallersOnly(EntryPoint = "GameNativeGetServerStubCatalogCount", CallConvs = [typeof(CallConvCdecl)])]
        public static int GameNativeGetServerStubCatalogCount(uint* count)
        {
            if (count == null)
            {
                return InvalidArgument;
            }

            try
            {
                *count = checked((uint)ServerStubCatalog.Entries.Count);
                return 0;
            }
            catch
            {
                *count = 0;
                return InvalidArgument;
            }
        }

        [UnmanagedCallersOnly(EntryPoint = "GameNativeGetServerStubCatalogEntry", CallConvs = [typeof(CallConvCdecl)])]
        public static int GameNativeGetServerStubCatalogEntry(uint index, ManagedServerStubCatalogEntry* entry)
        {
            if (entry == null || entry->StructSize < sizeof(ManagedServerStubCatalogEntry))
            {
                return InvalidArgument;
            }

            try
            {
                if (index >= (uint)ServerStubCatalog.Entries.Count)
                {
                    ResetCatalogEntry(entry);
                    return IndexOutOfRange;
                }

                ServerStubCatalogEntry catalogEntry = ServerStubCatalog.Entries[(int)index];
                ResetCatalogEntry(entry);

                int entityTypeResult = CopyUtf8(
                    catalogEntry.EntityTypeUtf8,
                    entry->EntityTypeUtf8,
                    ManagedAbi.ServerStubEntityTypeMaxUtf8Bytes,
                    &entry->EntityTypeLength);
                if (entityTypeResult != 0)
                {
                    return entityTypeResult;
                }

                int entityIdResult = CopyUtf8(
                    catalogEntry.EntityIdUtf8,
                    entry->EntityIdUtf8,
                    ManagedAbi.ServerStubEntityIdMaxUtf8Bytes,
                    &entry->EntityIdLength);
                if (entityIdResult != 0)
                {
                    return entityIdResult;
                }

                return 0;
            }
            catch
            {
                ResetCatalogEntry(entry);
                return InvalidArgument;
            }
        }

        private static IReadOnlyList<ServerStubEntity> GetReadyServerStubs()
        {
            if (s_runtimeState == null)
            {
                return Array.Empty<ServerStubEntity>();
            }

            return s_runtimeState.ReadyServerStubs;
        }

        private static AvatarEntitySpawnRequest BuildAvatarEntitySpawnRequest(byte* payload, uint payloadLength)
        {
            if (payload == null && payloadLength != 0)
            {
                throw new ArgumentException("Avatar create payload pointer is invalid.");
            }

            AvatarCreatePayload? request = JsonSerializer.Deserialize<AvatarCreatePayload>(
                new ReadOnlySpan<byte>(payload, checked((int)payloadLength)),
                ControlJsonOptions);
            if (request == null ||
                string.IsNullOrWhiteSpace(request.AccountId) ||
                string.IsNullOrWhiteSpace(request.AvatarId) ||
                string.IsNullOrWhiteSpace(request.GateNodeId) ||
                request.SessionId == 0)
            {
                throw new ArgumentException("Avatar create payload is incomplete.");
            }

            if (!Guid.TryParse(request.AvatarId, out Guid entityId) || entityId == Guid.Empty)
            {
                throw new ArgumentException("Avatar create payload avatarId must be a non-empty GUID.");
            }

            return new AvatarEntitySpawnRequest(
                entityId,
                request.AccountId,
                string.IsNullOrWhiteSpace(request.AvatarName) ? entityId.ToString("D") : request.AvatarName,
                request.GateNodeId,
                request.SessionId);
        }

        private static ServerStubOwnershipSnapshot BuildOwnershipSnapshot(ManagedServerStubOwnershipSync* sync)
        {
            if (sync == null || sync->StructSize < sizeof(ManagedServerStubOwnershipSync))
            {
                throw new ArgumentException("Ownership sync pointer is invalid.");
            }

            if (sync->AssignmentCount != 0 && sync->Assignments == null)
            {
                throw new ArgumentException("Ownership assignments pointer is invalid.");
            }

            ServerStubOwnershipAssignment[] assignments = new ServerStubOwnershipAssignment[sync->AssignmentCount];
            for (int index = 0; index < assignments.Length; ++index)
            {
                ManagedServerStubOwnershipEntry* entry = sync->Assignments + index;
                if (entry->StructSize < sizeof(ManagedServerStubOwnershipEntry))
                {
                    throw new ArgumentException("Ownership entry pointer is invalid.");
                }

                string entityType;
                string entityId;
                string ownerGameNodeId;

                byte* entityTypeBuffer = entry->EntityTypeUtf8;
                entityType = ReadUtf8(
                    entityTypeBuffer,
                    entry->EntityTypeLength,
                    ManagedAbi.ServerStubEntityTypeMaxUtf8Bytes);

                byte* entityIdBuffer = entry->EntityIdUtf8;
                entityId = ReadUtf8(
                    entityIdBuffer,
                    entry->EntityIdLength,
                    ManagedAbi.ServerStubEntityIdMaxUtf8Bytes);

                byte* ownerNodeIdBuffer = entry->OwnerGameNodeIdUtf8;
                ownerGameNodeId = ReadUtf8(
                    ownerNodeIdBuffer,
                    entry->OwnerGameNodeIdLength,
                    ManagedAbi.NodeIdMaxUtf8Bytes);

                assignments[index] = new ServerStubOwnershipAssignment(entityType, entityId, ownerGameNodeId);
            }

            return new ServerStubOwnershipSnapshot(sync->AssignmentEpoch, assignments);
        }

        private static void ResetCatalogEntry(ManagedServerStubCatalogEntry* entry)
        {
            entry->StructSize = (uint)sizeof(ManagedServerStubCatalogEntry);
            entry->EntityTypeLength = 0;
            entry->EntityIdLength = 0;
            entry->Reserved0 = 0;

            for (int index = 0; index < ManagedAbi.ServerStubEntityTypeMaxUtf8Bytes; ++index)
            {
                entry->EntityTypeUtf8[index] = 0;
            }

            for (int index = 0; index < ManagedAbi.ServerStubEntityIdMaxUtf8Bytes; ++index)
            {
                entry->EntityIdUtf8[index] = 0;
            }
        }

        private static void ResetReadyEntry(ManagedServerStubReadyEntry* entry)
        {
            entry->StructSize = (uint)sizeof(ManagedServerStubReadyEntry);
            entry->EntityTypeLength = 0;
            entry->EntityIdLength = 0;
            entry->Ready = 0;
            entry->EntryFlags = 0;

            for (int index = 0; index < ManagedAbi.ServerStubEntityTypeMaxUtf8Bytes; ++index)
            {
                entry->EntityTypeUtf8[index] = 0;
            }

            for (int index = 0; index < ManagedAbi.ServerStubEntityIdMaxUtf8Bytes; ++index)
            {
                entry->EntityIdUtf8[index] = 0;
            }

            for (int index = 0; index < 3; ++index)
            {
                entry->Reserved0[index] = 0;
            }
        }

        private static void NotifyNativeServerStubReady(ulong assignmentEpoch, ServerStubEntity stub)
        {
            if (s_nativeCallbacks.OnServerStubReady == null)
            {
                return;
            }

            ManagedServerStubReadyEntry entry = default;
            ResetReadyEntry(&entry);

            try
            {
                if (WriteReadyEntry(stub, &entry) != 0)
                {
                    return;
                }

                NativeLoggerBridge.Debug(RuntimeLogCategory, "Game managed runtime published server stub ready.");
                s_nativeCallbacks.OnServerStubReady(
                    s_nativeCallbacks.Context,
                    assignmentEpoch,
                    &entry);
            }
            catch
            {
            }
        }

        private static int WriteReadyEntry(ServerStubEntity stub, ManagedServerStubReadyEntry* entry)
        {
            byte[] entityTypeUtf8 = Encoding.UTF8.GetBytes(stub.EntityType);
            byte[] entityIdUtf8 = Encoding.UTF8.GetBytes(stub.EntityId.ToString());

            byte* entityTypeBuffer = entry->EntityTypeUtf8;
            int entityTypeResult = CopyUtf8(
                entityTypeUtf8,
                entityTypeBuffer,
                ManagedAbi.ServerStubEntityTypeMaxUtf8Bytes,
                &entry->EntityTypeLength);
            if (entityTypeResult != 0)
            {
                return entityTypeResult;
            }

            byte* entityIdBuffer = entry->EntityIdUtf8;
            int entityIdResult = CopyUtf8(
                entityIdUtf8,
                entityIdBuffer,
                ManagedAbi.ServerStubEntityIdMaxUtf8Bytes,
                &entry->EntityIdLength);
            if (entityIdResult != 0)
            {
                return entityIdResult;
            }

            entry->Ready = stub.IsReady ? (byte)1 : (byte)0;
            entry->EntryFlags = 0;
            return 0;
        }

        private static int CopyUtf8(byte[] source, byte* destination, int capacity, uint* outputLength)
        {
            if (outputLength == null)
            {
                return InvalidArgument;
            }

            if (source.Length > capacity)
            {
                *outputLength = 0;
                return BufferTooSmall;
            }

            for (int index = 0; index < source.Length; ++index)
            {
                destination[index] = source[index];
            }

            for (int index = source.Length; index < capacity; ++index)
            {
                destination[index] = 0;
            }

            *outputLength = (uint)source.Length;
            return 0;
        }

        private static string ReadUtf8(byte* utf8, uint utf8Length)
        {
            if (utf8 == null)
            {
                if (utf8Length == 0)
                {
                    return string.Empty;
                }

                throw new ArgumentException("UTF-8 pointer is null.");
            }

            return Encoding.UTF8.GetString(new ReadOnlySpan<byte>(utf8, checked((int)utf8Length)));
        }

        private static string ReadUtf8(byte* utf8, uint utf8Length, int capacity)
        {
            if (utf8Length > capacity)
            {
                throw new ArgumentException("UTF-8 length exceeds capacity.");
            }

            return ReadUtf8(utf8, utf8Length);
        }

        private sealed class AvatarCreatePayload
        {
            public string? AccountId { get; init; }

            public string? AvatarId { get; init; }

            public string? AvatarName { get; init; }

            public string? GateNodeId { get; init; }

            public ulong SessionId { get; init; }
        }
    }
}

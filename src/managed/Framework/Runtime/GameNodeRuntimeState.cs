using XServer.Managed.Framework.Catalog;
using XServer.Managed.Framework.Entities;
using XServer.Managed.Framework.Interop;

namespace XServer.Managed.Framework.Runtime
{
    public delegate void ServerStubReadyCallback(ulong assignmentEpoch, ServerStubEntity stub);

    public sealed class GameNodeRuntimeState : IServerStubCaller
    {
        private readonly string _nodeId;
        private readonly ServerStubReadyCallback? _onServerStubReady;
        private readonly IStubCallTransport? _stubCallTransport;
        private readonly INativeTimerScheduler? _nativeTimerScheduler;
        private readonly EntityManager _entityManager = new();
        private readonly List<ServerStubEntity> _ownedServerStubs = [];
        private readonly Dictionary<string, ServerStubEntity> _ownedServerStubsByType =
            new(StringComparer.Ordinal);
        private readonly Dictionary<Guid, AvatarEntity> _avatarsByEntityId = [];
        private IReadOnlyList<ServerStubOwnershipAssignment> _ownershipAssignments = Array.Empty<ServerStubOwnershipAssignment>();

        public GameNodeRuntimeState(
            string nodeId,
            ServerStubReadyCallback? onServerStubReady = null,
            IStubCallTransport? stubCallTransport = null,
            INativeTimerScheduler? nativeTimerScheduler = null)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(nodeId);

            _nodeId = nodeId;
            _onServerStubReady = onServerStubReady;
            _stubCallTransport = stubCallTransport;
            _nativeTimerScheduler = nativeTimerScheduler;
        }

        public string NodeId => _nodeId;

        public EntityManager EntityManager => _entityManager;

        public ulong AssignmentEpoch { get; private set; }

        public IReadOnlyList<ServerStubOwnershipAssignment> OwnershipAssignments => _ownershipAssignments;

        public IReadOnlyList<ServerStubEntity> OwnedServerStubs => _ownedServerStubs.ToArray();

        public IReadOnlyList<ServerStubEntity> ReadyServerStubs =>
            _ownedServerStubs.Where(static stub => stub.IsReady).ToArray();

        public bool HasOwnedServerStubs => _ownedServerStubs.Count != 0;

        public IReadOnlyList<AvatarEntity> AvatarEntities => _avatarsByEntityId.Values.ToArray();

        public bool IsLocalReady =>
            _ownedServerStubs.Count != 0 &&
            _ownedServerStubs.All(static stub => stub.IsReady);

        internal StubCallErrorCode ReceiveStubCall(string targetStubType, StubCallMessage message)
        {
            if (string.IsNullOrWhiteSpace(targetStubType))
            {
                return StubCallErrorCode.InvalidArgument;
            }

            if (message.MsgId == 0)
            {
                return StubCallErrorCode.InvalidMessageId;
            }

            if (!_ownedServerStubsByType.TryGetValue(targetStubType, out ServerStubEntity? targetStub))
            {
                return StubCallErrorCode.UnknownTargetStub;
            }

            return targetStub.ReceiveStubCall(message);
        }

        void IServerStubCaller.CallStub(ServerEntity sourceEntity, string targetStubType, StubCallMessage message)
        {
            if (sourceEntity == null || string.IsNullOrWhiteSpace(targetStubType))
            {
                LogStubCallFailure(sourceEntity, targetStubType, message.MsgId, "Stub call argument is invalid.");
                return;
            }

            if (message.MsgId == 0)
            {
                LogStubCallFailure(sourceEntity, targetStubType, message.MsgId, "Stub call msgId must not be zero.");
                return;
            }

            if (!_entityManager.Contains(sourceEntity.EntityId))
            {
                LogStubCallFailure(sourceEntity, targetStubType, message.MsgId, "Source entity is not registered in runtime state.");
                return;
            }

            if (_ownedServerStubsByType.TryGetValue(targetStubType, out ServerStubEntity? localTarget))
            {
                StubCallErrorCode localResult = localTarget.ReceiveStubCall(message);
                if (localResult != StubCallErrorCode.None)
                {
                    LogStubCallFailure(
                        sourceEntity,
                        targetStubType,
                        message.MsgId,
                        $"Local delivery failed: {StubCallError.Message(localResult)}");
                }

                return;
            }

            if (!TryGetOwnedGameNodeId(targetStubType, out string targetGameNodeId))
            {
                LogStubCallFailure(
                    sourceEntity,
                    targetStubType,
                    message.MsgId,
                    StubCallError.Message(StubCallErrorCode.UnknownTargetStub));
                return;
            }

            if (string.Equals(targetGameNodeId, _nodeId, StringComparison.Ordinal))
            {
                LogStubCallFailure(
                    sourceEntity,
                    targetStubType,
                    message.MsgId,
                    "Target stub is owned by the local game node but no local instance is available.");
                return;
            }

            if (_stubCallTransport == null)
            {
                LogStubCallFailure(
                    sourceEntity,
                    targetStubType,
                    message.MsgId,
                    StubCallError.Message(StubCallErrorCode.TargetNodeUnavailable));
                return;
            }

            StubCallErrorCode remoteResult = _stubCallTransport.Forward(
                targetStubType,
                targetGameNodeId,
                message);
            if (remoteResult != StubCallErrorCode.None)
            {
                LogStubCallFailure(
                    sourceEntity,
                    targetStubType,
                    message.MsgId,
                    $"Remote delivery to {targetGameNodeId} failed: {StubCallError.Message(remoteResult)}");
            }
        }

        public GameNodeRuntimeStateErrorCode ApplyOwnership(ServerStubOwnershipSnapshot snapshot)
        {
            if (snapshot == null)
            {
                return GameNodeRuntimeStateErrorCode.InvalidArgument;
            }

            ServerStubOwnershipAssignment[] nextAssignments = snapshot.Assignments.ToArray();
            ServerStubOwnershipAssignment[] nextOwnedAssignments = nextAssignments
                .Where(assignment => assignment.OwnerGameNodeId == _nodeId)
                .ToArray();

            if (snapshot.AssignmentEpoch == AssignmentEpoch && HasEquivalentLocalOwnership(nextOwnedAssignments))
            {
                _ownershipAssignments = nextAssignments;
                return GameNodeRuntimeStateErrorCode.None;
            }

            List<ServerStubEntity> createdStubs = [];
            HashSet<Guid> stagedEntityIds = [];

            foreach (ServerStubOwnershipAssignment assignment in nextOwnedAssignments)
            {
                if (!ServerEntityCatalog.TryResolveStubType(assignment.EntityType, out Type? stubType) || stubType == null)
                {
                    DestroyEntities(createdStubs);
                    return GameNodeRuntimeStateErrorCode.UnknownStubType;
                }

                if (Activator.CreateInstance(stubType) is not ServerStubEntity stub)
                {
                    DestroyEntities(createdStubs);
                    return GameNodeRuntimeStateErrorCode.StubInstantiationFailed;
                }

                stub.SetReadyCallback(HandleServerStubReady);
                stub.SetStubCaller(this);
                stub.SetNativeTimerScheduler(_nativeTimerScheduler);

                if (!stagedEntityIds.Add(stub.EntityId) || _entityManager.Contains(stub.EntityId))
                {
                    stub.SetReadyCallback(null);
                    stub.SetStubCaller(null);
                    stub.SetNativeTimerScheduler(null);
                    stub.Destroy();
                    DestroyEntities(createdStubs);
                    return GameNodeRuntimeStateErrorCode.DuplicateEntityId;
                }

                createdStubs.Add(stub);
            }

            List<ServerStubEntity> registeredStubs = [];
            foreach (ServerStubEntity stub in createdStubs)
            {
                EntityManagerErrorCode registerResult = _entityManager.Register(stub);
                if (registerResult != EntityManagerErrorCode.None)
                {
                    foreach (ServerStubEntity registeredStub in registeredStubs)
                    {
                        _ = _entityManager.Unregister(registeredStub.EntityId);
                    }

                    DestroyEntities(createdStubs);
                    return registerResult == EntityManagerErrorCode.DuplicateEntityId
                        ? GameNodeRuntimeStateErrorCode.DuplicateEntityId
                        : GameNodeRuntimeStateErrorCode.InvalidArgument;
                }

                registeredStubs.Add(stub);
            }

            ClearOwnedServerStubs();

            _ownedServerStubs.Clear();
            _ownedServerStubsByType.Clear();
            _ownedServerStubs.AddRange(createdStubs);
            foreach (ServerStubEntity stub in _ownedServerStubs)
            {
                _ownedServerStubsByType.Add(stub.EntityType, stub);
            }

            _ownershipAssignments = nextAssignments;
            AssignmentEpoch = snapshot.AssignmentEpoch;
            foreach (ServerStubEntity stub in _ownedServerStubs)
            {
                stub.Activate();
                _ = stub.TryMarkReady();
            }

            return GameNodeRuntimeStateErrorCode.None;
        }

        public void ResetOwnership()
        {
            ClearOwnedServerStubs();
            _ownershipAssignments = Array.Empty<ServerStubOwnershipAssignment>();
            AssignmentEpoch = 0;
        }

        public bool TryCreateAvatarEntity(
            AvatarEntitySpawnRequest request,
            out AvatarEntity? avatar,
            out string? error)
        {
            avatar = null;
            error = null;

            if (string.IsNullOrWhiteSpace(request.AccountId) ||
                request.EntityId == Guid.Empty ||
                string.IsNullOrWhiteSpace(request.RouteGateNodeId) ||
                request.SessionId == 0)
            {
                error = "Avatar spawn request is incomplete.";
                return false;
            }

            if (_avatarsByEntityId.TryGetValue(request.EntityId, out AvatarEntity? existingAvatar))
            {
                if (!string.Equals(existingAvatar.AccountId, request.AccountId, StringComparison.Ordinal))
                {
                    error = $"Avatar entity '{request.EntityId:D}' is already owned by another account.";
                    return false;
                }

                OnlineAvatarRegistration existingRegistration = new(
                    existingAvatar.AccountId,
                    existingAvatar.EntityId,
                    request.SessionId,
                    request.RouteGateNodeId,
                    _nodeId,
                    existingAvatar.DisplayName);
                if (!OnlineStub.TryRegisterAvatar(existingRegistration, out string existingRegisterError))
                {
                    error = existingRegisterError;
                    return false;
                }

                avatar = existingAvatar;
                return true;
            }

            AvatarEntity createdAvatar = new();
            createdAvatar.BindIdentity(
                request.EntityId,
                request.AccountId,
                request.AvatarName,
                request.RouteGateNodeId);
            createdAvatar.SetStubCaller(this);
            createdAvatar.SetNativeTimerScheduler(_nativeTimerScheduler);

            EntityManagerErrorCode registerResult = _entityManager.Register(createdAvatar);
            if (registerResult != EntityManagerErrorCode.None)
            {
                createdAvatar.SetStubCaller(null);
                createdAvatar.SetNativeTimerScheduler(null);
                error = EntityManagerError.Message(registerResult);
                return false;
            }

            OnlineAvatarRegistration registration = new(
                createdAvatar.AccountId,
                createdAvatar.EntityId,
                request.SessionId,
                request.RouteGateNodeId,
                _nodeId,
                createdAvatar.DisplayName);
            if (!OnlineStub.TryRegisterAvatar(registration, out string registerError))
            {
                _ = _entityManager.Unregister(createdAvatar.EntityId);
                createdAvatar.Destroy();
                createdAvatar.SetStubCaller(null);
                createdAvatar.SetNativeTimerScheduler(null);
                error = registerError;
                return false;
            }

            createdAvatar.Activate();
            _avatarsByEntityId.Add(createdAvatar.EntityId, createdAvatar);
            avatar = createdAvatar;
            return true;
        }

        private void ClearOwnedServerStubs()
        {
            foreach (ServerStubEntity stub in _ownedServerStubs)
            {
                stub.SetReadyCallback(null);
                _ = _entityManager.Unregister(stub.EntityId);
                stub.Destroy();
                stub.SetStubCaller(null);
                stub.SetNativeTimerScheduler(null);
            }

            _ownedServerStubs.Clear();
            _ownedServerStubsByType.Clear();
        }

        private bool HasEquivalentLocalOwnership(IReadOnlyList<ServerStubOwnershipAssignment> nextOwnedAssignments)
        {
            if (_ownedServerStubs.Count != nextOwnedAssignments.Count)
            {
                return false;
            }

            for (int index = 0; index < nextOwnedAssignments.Count; ++index)
            {
                if (!string.Equals(
                        _ownedServerStubs[index].EntityType,
                        nextOwnedAssignments[index].EntityType,
                        StringComparison.Ordinal))
                {
                    return false;
                }
            }

            return true;
        }

        private static void DestroyEntities(IEnumerable<ServerStubEntity> stubs)
        {
            foreach (ServerStubEntity stub in stubs)
            {
                stub.SetReadyCallback(null);
                stub.Destroy();
                stub.SetStubCaller(null);
                stub.SetNativeTimerScheduler(null);
            }
        }

        private bool TryGetOwnedGameNodeId(string targetStubType, out string ownerGameNodeId)
        {
            foreach (ServerStubOwnershipAssignment assignment in _ownershipAssignments)
            {
                if (string.Equals(assignment.EntityType, targetStubType, StringComparison.Ordinal))
                {
                    ownerGameNodeId = assignment.OwnerGameNodeId;
                    return true;
                }
            }

            ownerGameNodeId = string.Empty;
            return false;
        }

        private void HandleServerStubReady(ServerStubEntity stub)
        {
            _onServerStubReady?.Invoke(AssignmentEpoch, stub);
        }

        private static void LogStubCallFailure(
            ServerEntity? sourceEntity,
            string? targetStubType,
            uint msgId,
            string message)
        {
            string sourceEntityType = sourceEntity?.EntityType ?? "UnknownEntity";
            string resolvedTargetStubType = string.IsNullOrWhiteSpace(targetStubType) ? "<empty>" : targetStubType;
            NativeLoggerBridge.Warn(
                sourceEntityType,
                $"CallStub target={resolvedTargetStubType} msgId={msgId} failed: {message}");
        }
    }
}

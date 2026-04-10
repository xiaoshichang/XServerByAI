using XServer.Managed.Framework.Catalog;
using XServer.Managed.Framework.Entities;
using XServer.Managed.Framework.Interop;

namespace XServer.Managed.Framework.Runtime
{
    public delegate void ServerStubReadyCallback(ulong assignmentEpoch, ServerStubEntity stub);

    public sealed class GameNodeRuntimeState : IServerEntityMessageSender
    {
        private readonly string _nodeId;
        private readonly ServerStubReadyCallback? _onServerStubReady;
        private readonly IServerEntityMessageTransport? _messageTransport;
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
            IServerEntityMessageTransport? messageTransport = null,
            INativeTimerScheduler? nativeTimerScheduler = null)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(nodeId);

            _nodeId = nodeId;
            _onServerStubReady = onServerStubReady;
            _messageTransport = messageTransport;
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
            MailboxCallErrorCode mailboxResult = ReceiveMailboxCall(
                targetStubType,
                new MailboxCallMessage(message.MsgId, message.Payload));
            return mailboxResult switch
            {
                MailboxCallErrorCode.None => StubCallErrorCode.None,
                MailboxCallErrorCode.InvalidArgument => StubCallErrorCode.InvalidArgument,
                MailboxCallErrorCode.InvalidMessageId => StubCallErrorCode.InvalidMessageId,
                MailboxCallErrorCode.UnknownTargetMailbox => StubCallErrorCode.UnknownTargetStub,
                MailboxCallErrorCode.TargetNodeUnavailable => StubCallErrorCode.TargetNodeUnavailable,
                MailboxCallErrorCode.MailboxRejected => StubCallErrorCode.StubRejected,
                _ => StubCallErrorCode.StubRejected,
            };
        }

        internal MailboxCallErrorCode ReceiveMailboxCall(string targetMailboxName, MailboxCallMessage message)
        {
            return ReceiveMailboxCall(Guid.Empty, targetMailboxName, message);
        }

        internal MailboxCallErrorCode ReceiveMailboxCall(Guid targetEntityId, string targetMailboxName, MailboxCallMessage message)
        {
            if (targetEntityId == Guid.Empty && string.IsNullOrWhiteSpace(targetMailboxName))
            {
                return MailboxCallErrorCode.InvalidArgument;
            }

            if (message.MsgId == 0)
            {
                return MailboxCallErrorCode.InvalidMessageId;
            }

            if (targetEntityId != Guid.Empty)
            {
                if (_entityManager.TryGet(targetEntityId, out ServerEntity? targetEntity) && targetEntity != null)
                {
                    return targetEntity.ReceiveMailboxCall(message);
                }

                if (string.IsNullOrWhiteSpace(targetMailboxName))
                {
                    return MailboxCallErrorCode.UnknownTargetMailbox;
                }
            }

            if (Guid.TryParse(targetMailboxName, out Guid entityId) && entityId != Guid.Empty)
            {
                if (!_entityManager.TryGet(entityId, out ServerEntity? targetEntity) || targetEntity == null)
                {
                    return MailboxCallErrorCode.UnknownTargetMailbox;
                }

                return targetEntity.ReceiveMailboxCall(message);
            }

            if (_ownedServerStubsByType.TryGetValue(targetMailboxName, out ServerStubEntity? targetStub))
            {
                return targetStub.ReceiveMailboxCall(message);
            }

            return MailboxCallErrorCode.UnknownTargetMailbox;
        }

        internal ProxyCallErrorCode ReceiveProxyCall(Guid targetEntityId, ProxyCallMessage message)
        {
            if (targetEntityId == Guid.Empty)
            {
                return ProxyCallErrorCode.InvalidArgument;
            }

            if (message.MsgId == 0)
            {
                return ProxyCallErrorCode.InvalidMessageId;
            }

            if (!_entityManager.TryGet(targetEntityId, out ServerEntity? targetEntity) || targetEntity == null)
            {
                return ProxyCallErrorCode.UnknownTargetEntity;
            }

            return targetEntity.ReceiveProxyCall(message);
        }

        void IServerEntityMessageSender.CallStub(ServerEntity sourceEntity, string targetStubType, StubCallMessage message)
        {
            StubCallErrorCode result = DispatchStubCall(sourceEntity, targetStubType, message, out string failureMessage);
            if (result != StubCallErrorCode.None)
            {
                LogStubCallFailure(sourceEntity, targetStubType, message.MsgId, failureMessage);
            }
        }

        void IServerEntityMessageSender.CallMailbox(ServerEntity sourceEntity, MailboxAddress targetAddress, MailboxCallMessage message)
        {
            MailboxCallErrorCode result = DispatchMailboxCall(sourceEntity, targetAddress, message, out string failureMessage);
            if (result != MailboxCallErrorCode.None)
            {
                LogMailboxCallFailure(sourceEntity, targetAddress, message.MsgId, failureMessage);
            }
        }

        void IServerEntityMessageSender.CallServerProxy(ServerEntity sourceEntity, ProxyAddress targetAddress, ProxyCallMessage message)
        {
            if (sourceEntity == null ||
                targetAddress == null ||
                targetAddress.EntityId == Guid.Empty ||
                string.IsNullOrWhiteSpace(targetAddress.RouteGateNodeId))
            {
                LogProxyCallFailure(sourceEntity, targetAddress, message.MsgId, "Proxy call argument is invalid.");
                return;
            }

            if (message.MsgId == 0)
            {
                LogProxyCallFailure(sourceEntity, targetAddress, message.MsgId, "Proxy call msgId must not be zero.");
                return;
            }

            if (!_entityManager.Contains(sourceEntity.EntityId))
            {
                LogProxyCallFailure(sourceEntity, targetAddress, message.MsgId, "Source entity is not registered in runtime state.");
                return;
            }

            if (_entityManager.TryGet(targetAddress.EntityId, out ServerEntity? localTarget) && localTarget != null)
            {
                ProxyCallErrorCode localResult = localTarget.ReceiveProxyCall(message);
                if (localResult != ProxyCallErrorCode.None)
                {
                    LogProxyCallFailure(
                        sourceEntity,
                        targetAddress,
                        message.MsgId,
                        $"Local delivery failed: {ProxyCallError.Message(localResult)}");
                }

                return;
            }

            if (_messageTransport == null)
            {
                LogProxyCallFailure(
                    sourceEntity,
                    targetAddress,
                    message.MsgId,
                    ProxyCallError.Message(ProxyCallErrorCode.TargetNodeUnavailable));
                return;
            }

            ProxyCallErrorCode remoteResult = _messageTransport.ForwardByServerProxy(targetAddress, message);
            if (remoteResult != ProxyCallErrorCode.None)
            {
                LogProxyCallFailure(
                    sourceEntity,
                    targetAddress,
                    message.MsgId,
                    $"Remote delivery through {targetAddress.RouteGateNodeId} failed: {ProxyCallError.Message(remoteResult)}");
            }
        }

        void IServerEntityMessageSender.CallClient(ServerEntity sourceEntity, ProxyAddress targetAddress, ProxyCallMessage message)
        {
            if (sourceEntity == null ||
                targetAddress == null ||
                targetAddress.EntityId == Guid.Empty ||
                string.IsNullOrWhiteSpace(targetAddress.RouteGateNodeId))
            {
                LogClientPushFailure(sourceEntity, targetAddress, message.MsgId, "Client push argument is invalid.");
                return;
            }

            if (message.MsgId == 0)
            {
                LogClientPushFailure(sourceEntity, targetAddress, message.MsgId, "Client push msgId must not be zero.");
                return;
            }

            if (!_entityManager.Contains(sourceEntity.EntityId))
            {
                LogClientPushFailure(sourceEntity, targetAddress, message.MsgId, "Source entity is not registered in runtime state.");
                return;
            }

            if (_messageTransport == null)
            {
                LogClientPushFailure(sourceEntity, targetAddress, message.MsgId, ProxyCallError.Message(ProxyCallErrorCode.TargetNodeUnavailable));
                return;
            }

            ProxyCallErrorCode result = _messageTransport.ForwardByClientProxy(targetAddress, message);
            if (result != ProxyCallErrorCode.None)
            {
                LogClientPushFailure(
                    sourceEntity,
                    targetAddress,
                    message.MsgId,
                    $"Client delivery through {targetAddress.RouteGateNodeId} failed: {ProxyCallError.Message(result)}");
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
                stub.SetMessageSender(this);
                stub.SetNativeTimerScheduler(_nativeTimerScheduler);

                if (!stagedEntityIds.Add(stub.EntityId) || _entityManager.Contains(stub.EntityId))
                {
                    stub.SetReadyCallback(null);
                    stub.SetMessageSender(null);
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

                ProxyAddress reboundProxy = new(existingAvatar.EntityId, request.RouteGateNodeId);
                OnlineAvatarRegistration existingRegistration = new(
                    existingAvatar.AccountId,
                    existingAvatar.EntityId,
                    request.SessionId,
                    request.RouteGateNodeId,
                    _nodeId,
                    existingAvatar.DisplayName,
                    reboundProxy);
                if (!TryRegisterAvatarWithOnlineStub(existingAvatar, existingRegistration, out string existingRegisterError))
                {
                    error = existingRegisterError;
                    return false;
                }

                existingAvatar.RebindProxy(request.RouteGateNodeId);
                avatar = existingAvatar;
                return true;
            }

            AvatarEntity createdAvatar = new();
            createdAvatar.BindIdentity(
                request.EntityId,
                request.AccountId,
                request.AvatarName,
                request.RouteGateNodeId);
            createdAvatar.SetMessageSender(this);
            createdAvatar.SetNativeTimerScheduler(_nativeTimerScheduler);

            EntityManagerErrorCode registerResult = _entityManager.Register(createdAvatar);
            if (registerResult != EntityManagerErrorCode.None)
            {
                createdAvatar.SetMessageSender(null);
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
                createdAvatar.DisplayName,
                createdAvatar.Proxy!);
            if (!TryRegisterAvatarWithOnlineStub(createdAvatar, registration, out string registerError))
            {
                _ = _entityManager.Unregister(createdAvatar.EntityId);
                createdAvatar.Destroy();
                createdAvatar.SetMessageSender(null);
                createdAvatar.SetNativeTimerScheduler(null);
                error = registerError;
                return false;
            }

            createdAvatar.Activate();
            _avatarsByEntityId.Add(createdAvatar.EntityId, createdAvatar);
            avatar = createdAvatar;
            return true;
        }
        private bool TryRegisterAvatarWithOnlineStub(
            AvatarEntity sourceAvatar,
            OnlineAvatarRegistration registration,
            out string error)
        {
            byte[] payload;
            try
            {
                payload = OnlineAvatarRegistrationPayloadCodec.Encode(registration);
            }
            catch (ArgumentException exception)
            {
                error = exception.Message;
                return false;
            }

            StubCallErrorCode result = DispatchStubCall(
                sourceAvatar,
                nameof(OnlineStub),
                new StubCallMessage(OnlineStub.RegisterOnlineAvatarMessageStubMsgId, payload),
                out string failureMessage);
            if (result != StubCallErrorCode.None)
            {
                error = string.IsNullOrWhiteSpace(failureMessage)
                    ? StubCallError.Message(result)
                    : failureMessage;
                return false;
            }

            error = string.Empty;
            return true;
        }

        private MailboxCallErrorCode DispatchMailboxCall(
            ServerEntity sourceEntity,
            MailboxAddress targetAddress,
            MailboxCallMessage message,
            out string failureMessage)
        {
            failureMessage = string.Empty;

            if (sourceEntity == null ||
                targetAddress == null ||
                targetAddress.EntityId == Guid.Empty ||
                string.IsNullOrWhiteSpace(targetAddress.TargetGameNodeId))
            {
                failureMessage = "Mailbox call argument is invalid.";
                return MailboxCallErrorCode.InvalidArgument;
            }

            if (message.MsgId == 0)
            {
                failureMessage = "Mailbox call msgId must not be zero.";
                return MailboxCallErrorCode.InvalidMessageId;
            }

            if (!_entityManager.Contains(sourceEntity.EntityId))
            {
                failureMessage = "Source entity is not registered in runtime state.";
                return MailboxCallErrorCode.InvalidArgument;
            }

            if (_entityManager.TryGet(targetAddress.EntityId, out ServerEntity? localTarget) && localTarget != null)
            {
                MailboxCallErrorCode localResult = localTarget.ReceiveMailboxCall(message);
                if (localResult != MailboxCallErrorCode.None)
                {
                    failureMessage = $"Local delivery failed: {MailboxCallError.Message(localResult)}";
                }

                return localResult;
            }

            if (string.Equals(targetAddress.TargetGameNodeId, _nodeId, StringComparison.Ordinal))
            {
                failureMessage = "Target mailbox is owned by the local game node but no local instance is available.";
                return MailboxCallErrorCode.UnknownTargetMailbox;
            }

            if (_messageTransport == null)
            {
                failureMessage = MailboxCallError.Message(MailboxCallErrorCode.TargetNodeUnavailable);
                return MailboxCallErrorCode.TargetNodeUnavailable;
            }

            MailboxCallErrorCode remoteResult = _messageTransport.ForwardByMailbox(targetAddress, message);
            if (remoteResult != MailboxCallErrorCode.None)
            {
                failureMessage = $"Remote delivery to {targetAddress.TargetGameNodeId} failed: {MailboxCallError.Message(remoteResult)}";
            }

            return remoteResult;
        }

        private StubCallErrorCode DispatchStubCall(
            ServerEntity sourceEntity,
            string targetStubType,
            StubCallMessage message,
            out string failureMessage)
        {
            failureMessage = string.Empty;

            if (sourceEntity == null || string.IsNullOrWhiteSpace(targetStubType))
            {
                failureMessage = "Stub call argument is invalid.";
                return StubCallErrorCode.InvalidArgument;
            }

            if (message.MsgId == 0)
            {
                failureMessage = "Stub call msgId must not be zero.";
                return StubCallErrorCode.InvalidMessageId;
            }

            if (!_entityManager.Contains(sourceEntity.EntityId))
            {
                failureMessage = "Source entity is not registered in runtime state.";
                return StubCallErrorCode.InvalidArgument;
            }

            if (_ownedServerStubsByType.TryGetValue(targetStubType, out ServerStubEntity? localTarget))
            {
                StubCallErrorCode localResult = localTarget.ReceiveStubCall(message);
                if (localResult != StubCallErrorCode.None)
                {
                    failureMessage = $"Local delivery failed: {StubCallError.Message(localResult)}";
                }

                return localResult;
            }

            if (!TryGetOwnedGameNodeId(targetStubType, out string targetGameNodeId))
            {
                failureMessage = StubCallError.Message(StubCallErrorCode.UnknownTargetStub);
                return StubCallErrorCode.UnknownTargetStub;
            }

            if (string.Equals(targetGameNodeId, _nodeId, StringComparison.Ordinal))
            {
                failureMessage = "Target stub is owned by the local game node but no local instance is available.";
                return StubCallErrorCode.UnknownTargetStub;
            }

            if (_messageTransport == null)
            {
                failureMessage = StubCallError.Message(StubCallErrorCode.TargetNodeUnavailable);
                return StubCallErrorCode.TargetNodeUnavailable;
            }

            MailboxCallErrorCode mailboxResult = _messageTransport.ForwardByStubType(
                targetStubType,
                new MailboxCallMessage(message.MsgId, message.Payload));
            StubCallErrorCode remoteResult = ToStubCallErrorCode(mailboxResult);
            if (remoteResult != StubCallErrorCode.None)
            {
                failureMessage = $"Remote delivery to {targetGameNodeId} failed: {StubCallError.Message(remoteResult)}";
            }

            return remoteResult;
        }

        private void ClearOwnedServerStubs()
        {
            foreach (ServerStubEntity stub in _ownedServerStubs)
            {
                stub.SetReadyCallback(null);
                _ = _entityManager.Unregister(stub.EntityId);
                stub.Destroy();
                stub.SetMessageSender(null);
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
                stub.SetMessageSender(null);
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

        private static StubCallErrorCode ToStubCallErrorCode(MailboxCallErrorCode mailboxResult)
        {
            return mailboxResult switch
            {
                MailboxCallErrorCode.None => StubCallErrorCode.None,
                MailboxCallErrorCode.InvalidArgument => StubCallErrorCode.InvalidArgument,
                MailboxCallErrorCode.InvalidMessageId => StubCallErrorCode.InvalidMessageId,
                MailboxCallErrorCode.UnknownTargetMailbox => StubCallErrorCode.UnknownTargetStub,
                MailboxCallErrorCode.TargetNodeUnavailable => StubCallErrorCode.TargetNodeUnavailable,
                MailboxCallErrorCode.MailboxRejected => StubCallErrorCode.StubRejected,
                _ => StubCallErrorCode.StubRejected,
            };
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

        private static void LogMailboxCallFailure(
            ServerEntity? sourceEntity,
            MailboxAddress? targetAddress,
            uint msgId,
            string message)
        {
            string sourceEntityType = sourceEntity?.EntityType ?? "UnknownEntity";
            string targetEntityId = targetAddress?.EntityId.ToString("D") ?? "<empty>";
            string targetGameNodeId = targetAddress?.TargetGameNodeId ?? "<empty>";
            NativeLoggerBridge.Warn(
                sourceEntityType,
                $"CallMailbox entityId={targetEntityId} targetGameNodeId={targetGameNodeId} msgId={msgId} failed: {message}");
        }

        private static void LogProxyCallFailure(
            ServerEntity? sourceEntity,
            ProxyAddress? targetAddress,
            uint msgId,
            string message)
        {
            string sourceEntityType = sourceEntity?.EntityType ?? "UnknownEntity";
            string targetEntityId = targetAddress?.EntityId.ToString("D") ?? "<empty>";
            string routeGateNodeId = targetAddress?.RouteGateNodeId ?? "<empty>";
            NativeLoggerBridge.Warn(
                sourceEntityType,
                $"CallProxy entityId={targetEntityId} routeGateNodeId={routeGateNodeId} msgId={msgId} failed: {message}");
        }

        private static void LogClientPushFailure(
            ServerEntity? sourceEntity,
            ProxyAddress? targetAddress,
            uint msgId,
            string message)
        {
            string sourceEntityType = sourceEntity?.EntityType ?? "UnknownEntity";
            string targetEntityId = targetAddress?.EntityId.ToString("D") ?? "<empty>";
            string routeGateNodeId = targetAddress?.RouteGateNodeId ?? "<empty>";
            NativeLoggerBridge.Warn(
                sourceEntityType,
                $"PushToClient entityId={targetEntityId} routeGateNodeId={routeGateNodeId} msgId={msgId} failed: {message}");
        }
    }
}

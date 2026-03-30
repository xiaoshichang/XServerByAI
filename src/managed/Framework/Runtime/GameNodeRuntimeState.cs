using XServer.Managed.Framework.Catalog;
using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Runtime
{
    public delegate void ServerStubReadyCallback(ulong assignmentEpoch, ServerStubEntity stub);

    public sealed class GameNodeRuntimeState
    {
        private readonly string _nodeId;
        private readonly ServerStubReadyCallback? _onServerStubReady;
        private readonly EntityManager _entityManager = new();
        private readonly List<ServerStubEntity> _ownedServerStubs = [];
        private IReadOnlyList<ServerStubOwnershipAssignment> _ownershipAssignments = Array.Empty<ServerStubOwnershipAssignment>();

        public GameNodeRuntimeState(string nodeId, ServerStubReadyCallback? onServerStubReady = null)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(nodeId);

            _nodeId = nodeId;
            _onServerStubReady = onServerStubReady;
        }

        public string NodeId => _nodeId;

        public EntityManager EntityManager => _entityManager;

        public ulong AssignmentEpoch { get; private set; }

        public IReadOnlyList<ServerStubOwnershipAssignment> OwnershipAssignments => _ownershipAssignments;

        public IReadOnlyList<ServerStubEntity> OwnedServerStubs => _ownedServerStubs.ToArray();

        public IReadOnlyList<ServerStubEntity> ReadyServerStubs =>
            _ownedServerStubs.Where(static stub => stub.IsReady).ToArray();

        public bool HasOwnedServerStubs => _ownedServerStubs.Count != 0;

        public bool IsLocalReady =>
            _ownedServerStubs.Count != 0 &&
            _ownedServerStubs.All(static stub => stub.IsReady);

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

                if (!stagedEntityIds.Add(stub.EntityId) || _entityManager.Contains(stub.EntityId))
                {
                    stub.SetReadyCallback(null);
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
            _ownedServerStubs.AddRange(createdStubs);
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

        private void ClearOwnedServerStubs()
        {
            foreach (ServerStubEntity stub in _ownedServerStubs)
            {
                stub.SetReadyCallback(null);
                _ = _entityManager.Unregister(stub.EntityId);
                stub.Destroy();
            }

            _ownedServerStubs.Clear();
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
            }
        }

        private void HandleServerStubReady(ServerStubEntity stub)
        {
            _onServerStubReady?.Invoke(AssignmentEpoch, stub);
        }
    }
}
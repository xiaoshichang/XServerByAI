namespace XServer.Managed.Framework.Runtime
{
    public sealed class ServerStubOwnershipSnapshot
    {
        public ServerStubOwnershipSnapshot(ulong assignmentEpoch, IEnumerable<ServerStubOwnershipAssignment> assignments)
        {
            ArgumentNullException.ThrowIfNull(assignments);

            AssignmentEpoch = assignmentEpoch;
            Assignments = assignments.ToArray();
        }

        public ulong AssignmentEpoch { get; }

        public IReadOnlyList<ServerStubOwnershipAssignment> Assignments { get; }
    }
}
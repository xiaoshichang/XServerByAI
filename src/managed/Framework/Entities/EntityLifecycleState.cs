namespace XServer.Managed.Framework.Entities
{
    /// <summary>
    /// Describes the local runtime stage of a managed entity instance inside a Game process.
    /// The state machine models only instance lifecycle boundaries. It does not itself perform
    /// routing changes, owner reassignment, persistence IO, or cross-node migration protocols.
    /// </summary>
    public enum EntityLifecycleState
    {
        /// <summary>
        /// The entity object has been constructed and received its stable identity, but it has not
        /// entered the active runtime yet. At this point it can still finish local initialization,
        /// but it must not be treated as available for normal message dispatch, ticking, or ownership-driven work.
        /// </summary>
        Constructed = 0,

        /// <summary>
        /// The entity is locally active and can participate in normal runtime behavior.
        /// This is the steady state for entities that are ready to process business logic on the current Game node.
        /// A migratable entity may transition from here into <see cref="Migrating" /> when a move is initiated.
        /// </summary>
        Active = 1,

        /// <summary>
        /// The entity is in the middle of a migration workflow.
        /// While in this state the instance is no longer in its ordinary steady state, and callers should treat it as
        /// being prepared for transfer or rollback rather than as a fully settled local owner.
        /// Only migratable entities may enter this state; pinned entities must never do so.
        /// </summary>
        Migrating = 2,

        /// <summary>
        /// The entity has been deactivated locally and is no longer participating in normal runtime behavior,
        /// but the instance has not yet been permanently destroyed.
        /// This state is useful for explicit shutdown or teardown paths where the object still exists,
        /// yet should not be considered an active business entity anymore.
        /// </summary>
        Deactivated = 3,

        /// <summary>
        /// The entity has reached its terminal lifecycle state.
        /// After entering this state the instance must be treated as dead: it cannot be reactivated,
        /// resumed, or used as a valid local runtime owner again.
        /// </summary>
        Destroyed = 4,
    }
}

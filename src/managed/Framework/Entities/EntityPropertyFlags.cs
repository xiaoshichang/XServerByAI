using System;

namespace XServer.Managed.Framework.Entities
{
    /// <summary>
    /// Describes the behavioral traits declared on an entity property.
    /// These flags are descriptive in the current milestone and will be consumed by
    /// later synchronization, persistence, or source-generated property access code.
    /// </summary>
    [Flags]
    public enum EntityPropertyFlags
    {
        /// <summary>
        /// The property is not synchronized to clients, but it still participates in migration.
        /// </summary>
        ServerOnly = 1 << 0,

        /// <summary>
        /// The property is synchronized to the owning client of the entity and participates in migration.
        /// </summary>
        ClientServer = 1 << 1,

        /// <summary>
        /// The property is synchronized to all relevant clients and participates in migration.
        /// </summary>
        AllClients = 1 << 2,

        /// <summary>
        /// The property should participate in persistence.
        /// </summary>
        Persistent = 1 << 3,
    }
}

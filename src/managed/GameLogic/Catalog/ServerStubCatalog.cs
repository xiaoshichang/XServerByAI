using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using XServer.Managed.Framework.Entities;

namespace XServer.Managed.GameLogic.Catalog
{
    public static class ServerStubCatalog
    {
        public const string UnknownEntityId = "unknown";

        private static readonly IReadOnlyList<ServerStubCatalogEntry> _entries = BuildEntries();

        public static IReadOnlyList<ServerStubCatalogEntry> Entries => _entries;

        private static IReadOnlyList<ServerStubCatalogEntry> BuildEntries()
        {
            return Assembly
                .GetExecutingAssembly()
                .GetExportedTypes()
                .Where(type =>
                    !type.IsAbstract &&
                    !type.ContainsGenericParameters &&
                    typeof(ServerStubEntity).IsAssignableFrom(type))
                .OrderBy(type => type.FullName, StringComparer.Ordinal)
                .Select(type => new ServerStubCatalogEntry(type.Name, UnknownEntityId))
                .ToArray();
        }
    }
}

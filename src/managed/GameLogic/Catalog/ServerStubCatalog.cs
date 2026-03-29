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

        private static readonly IReadOnlyList<Type> _stubTypes = DiscoverStubTypes();
        private static readonly IReadOnlyDictionary<string, Type> _stubTypesByEntityType =
            _stubTypes.ToDictionary(type => type.Name, StringComparer.Ordinal);
        private static readonly IReadOnlyList<ServerStubCatalogEntry> _entries = BuildEntries();

        public static IReadOnlyList<ServerStubCatalogEntry> Entries => _entries;

        public static bool TryResolveStubType(string entityType, out Type? stubType)
        {
            return _stubTypesByEntityType.TryGetValue(entityType, out stubType);
        }

        private static IReadOnlyList<Type> DiscoverStubTypes()
        {
            return Assembly
                .GetExecutingAssembly()
                .GetExportedTypes()
                .Where(type =>
                    !type.IsAbstract &&
                    !type.ContainsGenericParameters &&
                    typeof(ServerStubEntity).IsAssignableFrom(type))
                .OrderBy(type => type.FullName, StringComparer.Ordinal)
                .ToArray();
        }

        private static IReadOnlyList<ServerStubCatalogEntry> BuildEntries()
        {
            return _stubTypes
                .Select(type => new ServerStubCatalogEntry(type.Name, UnknownEntityId))
                .ToArray();
        }
    }
}

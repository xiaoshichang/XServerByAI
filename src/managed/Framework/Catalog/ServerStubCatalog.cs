namespace XServer.Managed.Framework.Catalog
{
    public static class ServerStubCatalog
    {
        public const string UnknownEntityId = "unknown";

        public static IReadOnlyList<ServerStubCatalogEntry> Entries => ServerEntityCatalog.StubTypes
            .Select(static type => new ServerStubCatalogEntry(type.Name, UnknownEntityId))
            .ToArray();

        public static bool TryResolveStubType(string entityType, out Type? stubType)
        {
            return ServerEntityCatalog.TryResolveStubType(entityType, out stubType);
        }
    }
}
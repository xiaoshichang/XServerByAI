namespace XServer.Managed.Framework.Reflection
{
    public static class ServerStubReflection
    {
        public const string UnknownEntityId = "unknown";

        public static IReadOnlyList<ServerStubReflectionEntry> Entries => ServerEntityReflection.StubTypes
            .Select(static type => new ServerStubReflectionEntry(type.Name, UnknownEntityId))
            .ToArray();

        public static bool TryResolveStubType(string entityType, out Type? stubType)
        {
            return ServerEntityReflection.TryResolveStubType(entityType, out stubType);
        }
    }
}

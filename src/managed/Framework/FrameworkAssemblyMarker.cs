using XServer.Managed.Foundation;

namespace XServer.Managed.Framework
{
    public static class FrameworkAssemblyMarker
    {
        public const string Name = "Framework";

        public static string DependencyName => FoundationAssemblyMarker.Name;
    }
}
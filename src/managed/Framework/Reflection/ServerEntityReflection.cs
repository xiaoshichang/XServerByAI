using System.Reflection;
using System.Runtime.Loader;
using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Reflection
{
    public static class ServerEntityReflection
    {
        public const string DiscoveryAssemblyPathsEnvironmentVariable = "XS_MANAGED_DISCOVERY_ASSEMBLY_PATHS";

        private static readonly object SyncRoot = new();
        private static string _loadedDiscoverySignature = string.Empty;
        private static IReadOnlyList<Type> _entityTypes = Array.Empty<Type>();
        private static IReadOnlyList<Type> _stubTypes = Array.Empty<Type>();
        private static IReadOnlyDictionary<string, Type> _entityTypesByName = new Dictionary<string, Type>(StringComparer.Ordinal);
        private static IReadOnlyDictionary<string, Type> _stubTypesByName = new Dictionary<string, Type>(StringComparer.Ordinal);

        public static IReadOnlyList<Type> EntityTypes
        {
            get
            {
                EnsureReflectionLoaded();
                return _entityTypes;
            }
        }

        public static IReadOnlyList<Type> StubTypes
        {
            get
            {
                EnsureReflectionLoaded();
                return _stubTypes;
            }
        }

        public static bool TryResolveEntityType(string entityType, out Type? type)
        {
            EnsureReflectionLoaded();
            return _entityTypesByName.TryGetValue(entityType, out type);
        }

        public static bool TryResolveStubType(string entityType, out Type? type)
        {
            EnsureReflectionLoaded();
            return _stubTypesByName.TryGetValue(entityType, out type);
        }

        private static void EnsureReflectionLoaded()
        {
            string discoverySignature = NormalizeDiscoverySignature(
                Environment.GetEnvironmentVariable(DiscoveryAssemblyPathsEnvironmentVariable));
            if (discoverySignature == _loadedDiscoverySignature && _entityTypes.Count != 0)
            {
                return;
            }

            lock (SyncRoot)
            {
                if (discoverySignature == _loadedDiscoverySignature && _entityTypes.Count != 0)
                {
                    return;
                }

                IReadOnlyList<Assembly> assemblies = LoadSearchAssemblies(discoverySignature);
                Type[] entityTypes = assemblies
                    .SelectMany(static assembly => GetConcreteExportedTypes(assembly, typeof(ServerEntity)))
                    .Distinct()
                    .OrderBy(static type => type.FullName, StringComparer.Ordinal)
                    .ToArray();
                Type[] stubTypes = entityTypes
                    .Where(static type => typeof(ServerStubEntity).IsAssignableFrom(type))
                    .ToArray();

                _entityTypes = entityTypes;
                _stubTypes = stubTypes;
                _entityTypesByName = entityTypes.ToDictionary(static type => type.Name, StringComparer.Ordinal);
                _stubTypesByName = stubTypes.ToDictionary(static type => type.Name, StringComparer.Ordinal);
                _loadedDiscoverySignature = discoverySignature;
            }
        }

        private static IReadOnlyList<Assembly> LoadSearchAssemblies(string discoverySignature)
        {
            Dictionary<string, Assembly> assembliesByLocation = new(StringComparer.OrdinalIgnoreCase);
            foreach (string assemblyPath in EnumerateCandidateAssemblyPaths(discoverySignature))
            {
                Assembly? assembly = TryLoadAssembly(assemblyPath);
                if (assembly == null)
                {
                    continue;
                }

                string normalizedLocation = NormalizeAssemblyLocation(assembly.Location);
                if (string.IsNullOrEmpty(normalizedLocation))
                {
                    normalizedLocation = NormalizeAssemblyLocation(assemblyPath);
                }

                if (!string.IsNullOrEmpty(normalizedLocation))
                {
                    assembliesByLocation[normalizedLocation] = assembly;
                }
            }

            return assembliesByLocation.Values
                .OrderBy(static assembly => assembly.FullName, StringComparer.Ordinal)
                .ToArray();
        }

        private static IEnumerable<string> EnumerateCandidateAssemblyPaths(string discoverySignature)
        {
            HashSet<string> yieldedPaths = new(StringComparer.OrdinalIgnoreCase);

            string currentAssemblyPath = NormalizeAssemblyLocation(typeof(ServerEntityReflection).Assembly.Location);
            if (!string.IsNullOrEmpty(currentAssemblyPath) && yieldedPaths.Add(currentAssemblyPath))
            {
                yield return currentAssemblyPath;
            }

            string? currentAssemblyDirectory = Path.GetDirectoryName(currentAssemblyPath);
            if (!string.IsNullOrEmpty(currentAssemblyDirectory) && Directory.Exists(currentAssemblyDirectory))
            {
                foreach (string candidatePath in Directory
                             .EnumerateFiles(currentAssemblyDirectory, "XServer.Managed.*.dll")
                             .Select(static path => NormalizeAssemblyLocation(path))
                             .OrderBy(static path => path, StringComparer.OrdinalIgnoreCase))
                {
                    if (!string.IsNullOrEmpty(candidatePath) && yieldedPaths.Add(candidatePath))
                    {
                        yield return candidatePath;
                    }
                }
            }

            foreach (string configuredPath in SplitDiscoveryPaths(discoverySignature))
            {
                string normalizedPath = NormalizeAssemblyLocation(configuredPath);
                if (!string.IsNullOrEmpty(normalizedPath) && yieldedPaths.Add(normalizedPath))
                {
                    yield return normalizedPath;
                }
            }
        }

        private static IEnumerable<string> SplitDiscoveryPaths(string discoverySignature)
        {
            return discoverySignature.Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        }

        private static string NormalizeDiscoverySignature(string? discoverySignature)
        {
            return discoverySignature?.Trim() ?? string.Empty;
        }

        private static string NormalizeAssemblyLocation(string? assemblyLocation)
        {
            if (string.IsNullOrWhiteSpace(assemblyLocation))
            {
                return string.Empty;
            }

            return Path.GetFullPath(assemblyLocation);
        }

        private static Assembly? TryLoadAssembly(string assemblyPath)
        {
            if (string.IsNullOrWhiteSpace(assemblyPath) || !File.Exists(assemblyPath))
            {
                return null;
            }

            string normalizedAssemblyPath = NormalizeAssemblyLocation(assemblyPath);

            foreach (Assembly loadedAssembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                if (string.Equals(
                        NormalizeAssemblyLocation(loadedAssembly.Location),
                        normalizedAssemblyPath,
                        StringComparison.OrdinalIgnoreCase))
                {
                    return loadedAssembly;
                }
            }

            try
            {
                AssemblyName candidateAssemblyName = AssemblyName.GetAssemblyName(normalizedAssemblyPath);
                foreach (Assembly loadedAssembly in AppDomain.CurrentDomain.GetAssemblies())
                {
                    if (AssemblyName.ReferenceMatchesDefinition(candidateAssemblyName, loadedAssembly.GetName()))
                    {
                        return loadedAssembly;
                    }
                }
            }
            catch
            {
            }

            try
            {
                // Native-hosted components are not guaranteed to live in AssemblyLoadContext.Default.
                AssemblyLoadContext loadContext =
                    AssemblyLoadContext.GetLoadContext(typeof(ServerEntityReflection).Assembly) ??
                    AssemblyLoadContext.Default;
                return loadContext.LoadFromAssemblyPath(normalizedAssemblyPath);
            }
            catch
            {
                return null;
            }
        }

        private static IEnumerable<Type> GetConcreteExportedTypes(Assembly assembly, Type baseType)
        {
            Type[] exportedTypes;
            try
            {
                exportedTypes = assembly.GetExportedTypes();
            }
            catch (ReflectionTypeLoadException exception)
            {
                exportedTypes = exception.Types
                    .Where(static type => type != null)
                    .Cast<Type>()
                    .ToArray();
            }
            catch
            {
                return Array.Empty<Type>();
            }

            return exportedTypes.Where(type =>
                !type.IsAbstract &&
                !type.ContainsGenericParameters &&
                baseType.IsAssignableFrom(type));
        }
    }
}

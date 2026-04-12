using System.Collections.Concurrent;
using System.Reflection;

#if XSERVER_CLIENT_FRAMEWORK
namespace XServer.Client.Rpc;
#elif XSERVER_SERVER_FRAMEWORK
namespace XServer.Managed.Framework.Rpc;
#else
#error EntityRpc shared sources must be compiled by a framework project.
#endif

public sealed class EntityRpcMethodBinding
{
    public EntityRpcMethodBinding(string rpcName, MethodInfo methodInfo, IReadOnlyList<Type> parameterTypes)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(rpcName);
        ArgumentNullException.ThrowIfNull(methodInfo);
        ArgumentNullException.ThrowIfNull(parameterTypes);

        RpcName = rpcName;
        MethodInfo = methodInfo;
        ParameterTypes = parameterTypes;
    }

    public string RpcName { get; }

    public MethodInfo MethodInfo { get; }

    public IReadOnlyList<Type> ParameterTypes { get; }

    public void Invoke(object target, object?[] arguments)
    {
        MethodInfo.Invoke(target, arguments);
    }
}

public static class EntityRpcMethodCatalog
{
    private static readonly ConcurrentDictionary<EntityRpcMethodCatalogKey, IReadOnlyDictionary<string, EntityRpcMethodBinding>> Cache = new();

    public static IReadOnlyDictionary<string, EntityRpcMethodBinding> GetOrAdd(Type entityType, Type attributeType)
    {
        ArgumentNullException.ThrowIfNull(entityType);
        ArgumentNullException.ThrowIfNull(attributeType);

        return Cache.GetOrAdd(
            new EntityRpcMethodCatalogKey(entityType, attributeType),
            static key => Scan(key.EntityType, key.AttributeType));
    }

    private static IReadOnlyDictionary<string, EntityRpcMethodBinding> Scan(Type entityType, Type attributeType)
    {
        if (!typeof(Attribute).IsAssignableFrom(attributeType))
        {
            throw new ArgumentException("RPC attribute type must derive from Attribute.", nameof(attributeType));
        }

        Dictionary<string, EntityRpcMethodBinding> bindings = new(StringComparer.Ordinal);
        MethodInfo[] methods = entityType.GetMethods(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
        foreach (MethodInfo method in methods)
        {
            if (!method.IsDefined(attributeType, inherit: true))
            {
                continue;
            }

            if (method.IsStatic)
            {
                throw new InvalidOperationException(
                    $"RPC method '{entityType.FullName}.{method.Name}' must be an instance method.");
            }

            if (method.ReturnType != typeof(void))
            {
                throw new InvalidOperationException(
                    $"RPC method '{entityType.FullName}.{method.Name}' must return void.");
            }

            if (method.IsGenericMethodDefinition || method.ContainsGenericParameters)
            {
                throw new InvalidOperationException(
                    $"RPC method '{entityType.FullName}.{method.Name}' must not be generic.");
            }

            ParameterInfo[] parameters = method.GetParameters();
            List<Type> parameterTypes = new(parameters.Length);
            foreach (ParameterInfo parameter in parameters)
            {
                if (parameter.ParameterType.IsByRef || parameter.IsOut)
                {
                    throw new InvalidOperationException(
                        $"RPC method '{entityType.FullName}.{method.Name}' must not use ref/out parameters.");
                }

                if (parameter.GetCustomAttribute<ParamArrayAttribute>() is not null)
                {
                    throw new InvalidOperationException(
                        $"RPC method '{entityType.FullName}.{method.Name}' must not use params arrays.");
                }

                if (!EntityRpcJsonCodec.IsSupportedParameterType(parameter.ParameterType))
                {
                    throw new InvalidOperationException(
                        $"RPC method '{entityType.FullName}.{method.Name}' parameter '{parameter.Name}' type '{parameter.ParameterType.FullName}' is not supported.");
                }

                parameterTypes.Add(parameter.ParameterType);
            }

            string rpcName = method.Name;
            if (bindings.ContainsKey(rpcName))
            {
                throw new InvalidOperationException(
                    $"RPC method name '{rpcName}' is duplicated on entity type '{entityType.FullName}'.");
            }

            bindings.Add(rpcName, new EntityRpcMethodBinding(rpcName, method, parameterTypes));
        }

        return bindings;
    }

    private readonly record struct EntityRpcMethodCatalogKey(Type EntityType, Type AttributeType);
}

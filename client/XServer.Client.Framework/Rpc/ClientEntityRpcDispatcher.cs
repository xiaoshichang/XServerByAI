using System.Reflection;
using XServer.Client.Entities;
using XServer.Client.Runtime;
using XServer.Managed.Foundation.Rpc;

namespace XServer.Client.Rpc;

internal static class ClientEntityRpcDispatcher
{
    public static EntityRpcDispatchErrorCode Dispatch(
        ClientRuntimeState state,
        ReadOnlyMemory<byte> payload,
        out Guid entityId,
        out string rpcName,
        out string errorMessage)
    {
        ArgumentNullException.ThrowIfNull(state);

        entityId = Guid.Empty;
        rpcName = string.Empty;
        errorMessage = string.Empty;

        if (!EntityRpcJsonCodec.TryDecode(
                payload,
                out EntityRpcInvocationEnvelope envelope,
                out EntityRpcDispatchErrorCode decodeError,
                out errorMessage))
        {
            return decodeError;
        }

        entityId = envelope.EntityId;
        rpcName = envelope.RpcName;

        if (!state.EntityManager.TryGet(envelope.EntityId, out ClientEntity? entity) || entity is null)
        {
            errorMessage = $"Client RPC target entity '{envelope.EntityId:D}' could not be found.";
            return EntityRpcDispatchErrorCode.EntityNotFound;
        }

        IReadOnlyDictionary<string, EntityRpcMethodBinding> bindings = GetBindings(entity.GetType());
        if (!bindings.TryGetValue(envelope.RpcName, out EntityRpcMethodBinding? binding))
        {
            errorMessage = $"Client RPC '{envelope.RpcName}' is not defined on '{entity.GetType().FullName}'.";
            return EntityRpcDispatchErrorCode.UnknownRpc;
        }

        if (!EntityRpcJsonCodec.TryBindArguments(
                envelope,
                binding.ParameterTypes,
                out object?[] arguments,
                out EntityRpcDispatchErrorCode bindError,
                out errorMessage))
        {
            return bindError;
        }

        try
        {
            binding.Invoke(entity, arguments);
            return EntityRpcDispatchErrorCode.None;
        }
        catch (TargetInvocationException exception)
        {
            Exception? innerException = exception.InnerException ?? exception;
            errorMessage =
                $"Client RPC '{envelope.RpcName}' failed on '{entity.GetType().FullName}': {innerException.Message}";
            return EntityRpcDispatchErrorCode.TargetInvocationFailed;
        }
    }

    private static IReadOnlyDictionary<string, EntityRpcMethodBinding> GetBindings(Type entityType)
    {
        if (!typeof(ClientEntity).IsAssignableFrom(entityType))
        {
            throw new InvalidOperationException(
                $"Client RPC target type '{entityType.FullName}' does not derive from ClientEntity.");
        }

        return EntityRpcMethodCatalog.GetOrAdd(entityType, typeof(ClientRPCAttribute));
    }
}

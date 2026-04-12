using System.Reflection;
using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Rpc
{
    internal static class ServerEntityRpcDispatcher
    {
        public static EntityRpcDispatchErrorCode Dispatch(
            ServerEntity entity,
            ReadOnlyMemory<byte> payload,
            out string rpcName,
            out string errorMessage)
        {
            ArgumentNullException.ThrowIfNull(entity);

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

            rpcName = envelope.RpcName;
            if (envelope.EntityId != entity.EntityId)
            {
                errorMessage =
                    $"Server RPC target entity '{envelope.EntityId:D}' does not match '{entity.EntityId:D}'.";
                return EntityRpcDispatchErrorCode.EntityNotFound;
            }

            IReadOnlyDictionary<string, EntityRpcMethodBinding> bindings = GetBindings(entity.GetType());
            if (!bindings.TryGetValue(envelope.RpcName, out EntityRpcMethodBinding? binding))
            {
                errorMessage = $"Server RPC '{envelope.RpcName}' is not defined on '{entity.GetType().FullName}'.";
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
                    $"Server RPC '{envelope.RpcName}' failed on '{entity.GetType().FullName}': {innerException.Message}";
                return EntityRpcDispatchErrorCode.TargetInvocationFailed;
            }
        }

        private static IReadOnlyDictionary<string, EntityRpcMethodBinding> GetBindings(Type entityType)
        {
            if (!typeof(ServerEntity).IsAssignableFrom(entityType))
            {
                throw new InvalidOperationException(
                    $"Server RPC target type '{entityType.FullName}' does not derive from ServerEntity.");
            }

            return EntityRpcMethodCatalog.GetOrAdd(entityType, typeof(ServerRPCAttribute));
        }
    }
}

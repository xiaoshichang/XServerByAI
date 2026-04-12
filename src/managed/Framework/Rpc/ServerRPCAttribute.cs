namespace XServer.Managed.Framework.Rpc;

[AttributeUsage(AttributeTargets.Method, Inherited = true, AllowMultiple = false)]
public class ServerRPCAttribute : Attribute
{
}

[AttributeUsage(AttributeTargets.Method, Inherited = true, AllowMultiple = false)]
public sealed class ServerRpcAttribute : ServerRPCAttribute
{
}

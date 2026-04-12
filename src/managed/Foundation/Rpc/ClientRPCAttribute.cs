namespace XServer.Managed.Foundation.Rpc;

[AttributeUsage(AttributeTargets.Method, Inherited = true, AllowMultiple = false)]
public class ClientRPCAttribute : Attribute
{
}

[AttributeUsage(AttributeTargets.Method, Inherited = true, AllowMultiple = false)]
public sealed class ClientRpcAttribute : ClientRPCAttribute
{
}

namespace XServer.Managed.Framework.Entities
{
    // Stub entities represent shared services whose owner game node is assigned by GM during startup.
    public abstract class ServerStubEntity : ServerEntity
    {
        public override bool IsMigratable()
        {
            return false;
        }
    }
}

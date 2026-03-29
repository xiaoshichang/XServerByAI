namespace XServer.Managed.Framework.Entities
{
    // Stub entities represent shared services whose owner game node is assigned by GM during startup.
    public abstract partial class ServerStubEntity : ServerEntity
    {
        [EntityProperty(EntityPropertyFlags.ServerOnly)]
        protected int __TestField = 1;
        
        public override bool IsMigratable()
        {
            return false;
        }
    }
}

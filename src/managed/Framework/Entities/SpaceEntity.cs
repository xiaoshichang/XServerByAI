namespace XServer.Managed.Framework.Entities
{
    public sealed class SpaceEntity : ServerEntity
    {
        public override bool IsMigratable()
        {
            return false;
        }
    }
}

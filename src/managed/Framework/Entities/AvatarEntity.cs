namespace XServer.Managed.Framework.Entities
{
    public sealed class AvatarEntity : ServerEntity
    {
        public override bool IsMigratable()
        {
            return true;
        }
    }
}

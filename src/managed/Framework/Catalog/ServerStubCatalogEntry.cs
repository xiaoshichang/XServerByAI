using System.Text;

namespace XServer.Managed.Framework.Catalog
{
    public sealed class ServerStubCatalogEntry
    {
        public ServerStubCatalogEntry(string entityType, string entityId)
        {
            EntityType = entityType;
            EntityId = entityId;
            EntityTypeUtf8 = Encoding.UTF8.GetBytes(entityType);
            EntityIdUtf8 = Encoding.UTF8.GetBytes(entityId);
        }

        public string EntityType { get; }

        public string EntityId { get; }

        public byte[] EntityTypeUtf8 { get; }

        public byte[] EntityIdUtf8 { get; }
    }
}
using System.Linq;
using XServer.Managed.Framework.Catalog;
using XServer.Managed.Framework.Entities;
using XServer.Managed.GameLogic.Services;

namespace XServer.Managed.Framework.Tests
{
    public class FrameworkBaselineTests
    {
        [Fact]
        public void ServerStubEntity_InheritsFromServerEntity()
        {
            Assert.True(typeof(ServerStubEntity).IsSubclassOf(typeof(ServerEntity)));
        }

        [Fact]
        public void EntityAddresses_PreserveGuidIdentity_AndPreferLocalDispatchFirst()
        {
            Guid entityId = Guid.NewGuid();

            EntityAddress mailbox = new MailboxAddress(entityId, "Game0");
            EntityAddress proxy = new ProxyAddress(entityId, "Gate0");

            Assert.Equal(entityId, mailbox.EntityId);
            Assert.Equal(entityId, proxy.EntityId);
            Assert.True(mailbox.ShouldAttemptLocalDispatchFirst);
            Assert.True(proxy.ShouldAttemptLocalDispatchFirst);
        }

        [Fact]
        public void MailboxAddress_CarriesStaticTargetGameNodeId()
        {
            MailboxAddress mailbox = new(Guid.NewGuid(), "Game1");

            Assert.Equal("Game1", mailbox.TargetGameNodeId);
        }

        [Fact]
        public void ProxyAddress_CarriesRouteGateNodeId()
        {
            ProxyAddress proxy = new(Guid.NewGuid(), "Gate2");

            Assert.Equal("Gate2", proxy.RouteGateNodeId);
        }

        [Fact]
        public void FrameworkAssemblyMarker_DependsOnFoundation()
        {
            Assert.Equal("Foundation", FrameworkAssemblyMarker.DependencyName);
        }

        [Fact]
        public void ServerEntity_AssignsUniqueGuidIds()
        {
            var first = new TestEntity();
            var second = new TestEntity();
            var stub = new TestStubEntity();

            Assert.NotEqual(Guid.Empty, first.EntityId);
            Assert.NotEqual(Guid.Empty, second.EntityId);
            Assert.NotEqual(Guid.Empty, stub.EntityId);
            Assert.NotEqual(first.EntityId, second.EntityId);
            Assert.NotEqual(first.EntityId, stub.EntityId);
        }

        [Fact]
        public void ServerEntityCatalog_ReturnsConcreteEntityTypesAcrossFrameworkAndGameLogicAssemblies()
        {
            var entityTypeNames = ServerEntityCatalog.EntityTypes
                .Select(static type => type.Name)
                .ToArray();

            Assert.Contains(nameof(OnlineStub), entityTypeNames);
            Assert.Contains(nameof(ChatStub), entityTypeNames);
            Assert.Contains(nameof(LeaderboardStub), entityTypeNames);
            Assert.Contains(nameof(MatchStub), entityTypeNames);
            Assert.DoesNotContain(nameof(ServerEntity), entityTypeNames);
            Assert.DoesNotContain(nameof(ServerStubEntity), entityTypeNames);
        }

        [Fact]
        public void ServerStubCatalog_ProjectsDiscoveredStubTypesWithDefaultUnknownEntityIds()
        {
            var entries = ServerStubCatalog.Entries.ToArray();
            var entryTypeNames = entries
                .Select(static entry => entry.EntityType)
                .ToArray();
            var discoveredStubTypeNames = ServerEntityCatalog.StubTypes
                .Select(static type => type.Name)
                .ToArray();

            Assert.NotEmpty(entries);
            Assert.Equal(discoveredStubTypeNames, entryTypeNames);
            Assert.All(entries, static entry => Assert.Equal(ServerStubCatalog.UnknownEntityId, entry.EntityId));
            Assert.Contains(nameof(OnlineStub), entryTypeNames);
            Assert.Contains(nameof(ChatStub), entryTypeNames);
            Assert.Contains(nameof(LeaderboardStub), entryTypeNames);
            Assert.Contains(nameof(MatchStub), entryTypeNames);
        }

        [Fact]
        public void ServerStubCatalog_DoesNotIncludeAbstractBaseType()
        {
            Assert.DoesNotContain(ServerStubCatalog.Entries, entry => entry.EntityType == nameof(ServerStubEntity));
        }

        [Fact]
        public void DerivedTypes_ExpressEntityCategoryThroughTypeHierarchy()
        {
            Assert.IsAssignableFrom<ServerEntity>(new TestEntity());
            Assert.IsAssignableFrom<ServerEntity>(new TestStubEntity());
            Assert.IsAssignableFrom<ServerStubEntity>(new TestStubEntity());
        }

        private sealed class TestEntity : ServerEntity
        {
        }

        private sealed class TestStubEntity : ServerStubEntity
        {
        }
    }
}

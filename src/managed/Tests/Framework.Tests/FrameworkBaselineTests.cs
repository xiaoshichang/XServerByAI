using XServer.Managed.Framework;
using XServer.Managed.Framework.Entities;
using XServer.Managed.GameLogic;

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
        public void GameLogicAssemblyMarker_DependsOnFramework()
        {
            Assert.Equal(FrameworkAssemblyMarker.Name, GameLogicAssemblyMarker.DependencyName);
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
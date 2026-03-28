namespace XServer.Managed.Framework.Entities
{
    public abstract partial class ServerEntity
    {
        public EntityLifecycleState LifecycleState { get; private set; }

        public void Activate()
        {
            TransitionTo(EntityLifecycleState.Active);
            OnActivated();
        }

        public void BeginMigration()
        {
            TransitionTo(EntityLifecycleState.Migrating);
            OnMigrationStarted();
        }

        public void CompleteMigration()
        {
            TransitionTo(EntityLifecycleState.Active);
            OnMigrationCompleted();
        }

        public void CancelMigration()
        {
            TransitionTo(EntityLifecycleState.Active);
            OnMigrationCancelled();
        }

        public void Deactivate()
        {
            TransitionTo(EntityLifecycleState.Deactivated);
            OnDeactivated();
        }

        public void Destroy()
        {
            TransitionTo(EntityLifecycleState.Destroyed);
            OnDestroyed();
        }

        protected virtual void OnActivated()
        {
        }

        protected virtual void OnMigrationStarted()
        {
        }

        protected virtual void OnMigrationCompleted()
        {
        }

        protected virtual void OnMigrationCancelled()
        {
        }

        protected virtual void OnDeactivated()
        {
        }

        protected virtual void OnDestroyed()
        {
        }

        private void TransitionTo(EntityLifecycleState nextState)
        {
            LifecycleState = nextState;
        }
    }
}

using System.Text;
using XServer.Managed.Framework.Interop;
using XServer.Managed.Framework.Runtime;

namespace XServer.Managed.Framework.Entities
{
    public sealed class OnlineStub : ServerStubEntity
    {
        private const string StartupTargetStubType = "MatchStub";
        private const uint StartupCallMsgId = 5101u;
        private long _startupTimerId;

        protected override void OnReady()
        {
            base.OnReady();

            if (_startupTimerId > 0)
            {
                return;
            }

            long timerId = CreateNativeOnceTimer(TimeSpan.FromSeconds(5), HandleStartupTimerFired);
            if (NativeTimerResult.IsTimerId(timerId))
            {
                _startupTimerId = timerId;
            }
        }

        protected override void OnDestroyed()
        {
            if (_startupTimerId > 0)
            {
                _ = CancelNativeTimer(_startupTimerId);
                _startupTimerId = 0;
            }
        }

        private void HandleStartupTimerFired()
        {
            _startupTimerId = 0;

            CallStub(
                StartupTargetStubType,
                StartupCallMsgId,
                Encoding.UTF8.GetBytes("online-startup-call"));
        }
    }
}

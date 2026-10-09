using System;
using System.Threading.Tasks;

namespace HumanVision
{
    // Sole owner until main-thread promotion. Cancellation also owns a late native success.
    internal sealed class HumanVisionStagedInitialization<T> where T : class, IDisposable
    {
        private readonly object gate = new object();
        private readonly Task<T> task;
        private bool abandoned, taken;
        internal HumanVisionStagedInitialization(Func<T> create)
        {
            task = Task.Run(create);
            task.ContinueWith(_ => ReleaseAbandoned(), TaskScheduler.Default);
        }
        internal bool Complete => task.IsCompleted;
        internal string Error => task.IsFaulted ? task.Exception.GetBaseException().Message : "";
        internal T Take()
        {
            lock(gate) {
                if (!task.IsCompleted || task.IsFaulted || task.IsCanceled || abandoned || taken) return null;
                taken = true; return task.Result;
            }
        }
        internal void Abandon() { lock(gate) abandoned = true; Task.Run((Action)ReleaseAbandoned); }
        private void ReleaseAbandoned()
        {
            T value = null;
            lock(gate) {
                if (abandoned && task.Status == TaskStatus.RanToCompletion && !taken) { taken = true; value = task.Result; }
                // Observe initialization exceptions even after the owner is disabled.
                if (task.IsFaulted) { var observed = task.Exception; }
            }
            value?.Dispose();
        }
    }
}

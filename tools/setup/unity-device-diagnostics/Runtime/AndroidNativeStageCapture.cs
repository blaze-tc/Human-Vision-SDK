using System;
using System.Threading;
using UnityEngine;

namespace HumanVision.TestProject.Diagnostics
{
    /// <summary>
    /// 性能诊断包专用：后台读取当前应用自己的 Android 原生日志。
    /// 只选择模型/输入计时标签，不读取其它应用日志，不改识别和 GPU 同步策略。
    /// Unity 对象及文件操作仍由主线程总控处理；读线程只提交有界队列。
    /// </summary>
    public sealed class AndroidNativeStageCapture : IDisposable
    {
        private readonly Action<string, int> enqueue;
        private readonly object processLock = new object();
        private volatile bool stopping;
        private Thread worker;
#if UNITY_ANDROID && !UNITY_EDITOR
        private AndroidJavaObject process;
#endif
        public AndroidNativeStageCapture(Action<string, int> enqueue)
        {
            this.enqueue = enqueue ?? throw new ArgumentNullException(nameof(enqueue));
        }
        /// <summary>
        /// 只采集本应用的模型、输入及 CPU 初始化配置。保留末尾静音过滤，避免其它标签
        /// 淹没诊断记录；此纯函数也可在 Editor 中验证，无需依赖 Android JNI。
        /// </summary>
        internal static string[] BuildLogcatCommand(int pid)
        {
            if (pid <= 0) throw new ArgumentOutOfRangeException(nameof(pid));
            // -T 1 从当前尾部开始，不清空系统缓冲；--pid 限制为本次应用进程。
            return new[] { "/system/bin/logcat", "-b", "main", "-v", "threadtime", "-T", "1", "--pid=" + pid,
                "HV_TOPDOWN_NCNN:V", "ncnn:V", "HVInputGate:I", "HumanVisionCpu:I", "*:S" };
        }
        public void Start()
        {
            if (worker != null) throw new InvalidOperationException("Native stage capture has already started.");
#if UNITY_ANDROID && !UNITY_EDITOR
            worker = new Thread(ReadAndroid) { IsBackground = true, Name = "HumanVision native stage capture" };
            worker.Start();
#else
            throw new PlatformNotSupportedException("Native stage capture requires an Android player.");
#endif
        }
#if UNITY_ANDROID && !UNITY_EDITOR
        private void ReadAndroid()
        {
            bool attached = false;
            try {
                // JNI 全局引用可跨线程使用，但工作线程必须先附加到 Java VM。
                if (AndroidJNI.AttachCurrentThread() != 0) throw new InvalidOperationException("Could not attach native log reader to Java VM.");
                attached = true;
                using (var androidProcess = new AndroidJavaClass("android.os.Process"))
                using (var runtimeClass = new AndroidJavaClass("java.lang.Runtime"))
                using (var runtime = runtimeClass.CallStatic<AndroidJavaObject>("getRuntime")) {
                    int pid = androidProcess.CallStatic<int>("myPid");
                    string[] command = BuildLogcatCommand(pid);
                    using (var ownedProcess = runtime.Call<AndroidJavaObject>("exec", new object[] { command })) {
                        lock (processLock) { process = ownedProcess; if (stopping) process.Call("destroy"); }
                        try {
                            using (var input = ownedProcess.Call<AndroidJavaObject>("getInputStream"))
                            using (var reader = new AndroidJavaObject("java.io.InputStreamReader", input))
                            using (var lines = new AndroidJavaObject("java.io.BufferedReader", reader)) {
                                enqueue("native.capture.started own_process_pid=" + pid, Thread.CurrentThread.ManagedThreadId);
                                while (!stopping) {
                                    string line = lines.Call<string>("readLine");
                                    if (line == null) break;
                                    if (!stopping) enqueue(line, Thread.CurrentThread.ManagedThreadId);
                                }
                                if (!stopping) enqueue("native.capture.ended unexpectedly; check Android logcat access.", Thread.CurrentThread.ManagedThreadId);
                            }
                        } finally {
                            // 释放 JNI 包装器不等于结束 OS 子进程，异常路径也必须 destroy。
                            lock (processLock) { process = null; try { ownedProcess.Call("destroy"); } catch { } }
                        }
                    }
                }
            } catch (Exception e) {
                if (!stopping) enqueue("native.capture.failed " + e.GetType().Name + ": " + e.Message, Thread.CurrentThread.ManagedThreadId);
            } finally {
                lock (processLock) { process = null; }
                if (attached) AndroidJNI.DetachCurrentThread();
            }
        }
#endif
        /// <summary>先结束 owned logcat 以解除阻塞读；最多等一秒，不无限阻塞 Unity 退出。</summary>
        public void Dispose()
        {
            stopping = true;
#if UNITY_ANDROID && !UNITY_EDITOR
            lock (processLock) {
                try { process?.Call("destroy"); } catch { /* 进程可能已经退出，工作线程记录真实读错误。 */ }
            }
#endif
            if (worker != null && worker != Thread.CurrentThread) worker.Join(1000);
        }
    }
}

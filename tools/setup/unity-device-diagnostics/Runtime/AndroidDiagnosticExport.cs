using System;
using System.IO;
using UnityEngine;

namespace HumanVision.TestProject.Diagnostics
{
    /// <summary>Android 10+ 通过 MediaStore 将 ZIP 写入 Download/HumanVisionLogs，不需要申请全盘存储权限。</summary>
    internal static class AndroidDiagnosticExport
    {
        public static bool TryCopyToDownloads(string zip, out string publicPath, out string error)
        {
            publicPath = ""; error = "";
#if UNITY_ANDROID && !UNITY_EDITOR
            AndroidJavaObject resolver = null, uri = null;
            bool complete = false;
            try {
                using (var version = new AndroidJavaClass("android.os.Build$VERSION"))
                    if (version.GetStatic<int>("SDK_INT") < 29) { error = "Android 10 以下保留在应用日志目录，请用复制 ZIP 路径获取。"; return false; }
                using (var unity = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
                using (var activity = unity.GetStatic<AndroidJavaObject>("currentActivity"))
                    resolver = activity.Call<AndroidJavaObject>("getContentResolver");
                using (var downloads = new AndroidJavaClass("android.provider.MediaStore$Downloads"))
                using (var collection = downloads.GetStatic<AndroidJavaObject>("EXTERNAL_CONTENT_URI"))
                using (var values = new AndroidJavaObject("android.content.ContentValues"))
                using (var one = new AndroidJavaObject("java.lang.Integer", 1)) {
                    values.Call("put", "_display_name", Path.GetFileName(zip)); values.Call("put", "mime_type", "application/zip");
                    values.Call("put", "relative_path", "Download/HumanVisionLogs"); values.Call("put", "is_pending", one);
                    uri = resolver.Call<AndroidJavaObject>("insert", collection, values);
                }
                if (uri == null) throw new IOException("MediaStore refused ZIP creation.");
                using (var output = resolver.Call<AndroidJavaObject>("openOutputStream", uri))
                using (var paths = new AndroidJavaClass("java.nio.file.Paths"))
                using (var source = paths.CallStatic<AndroidJavaObject>("get", zip, new string[0]))
                using (var files = new AndroidJavaClass("java.nio.file.Files")) {
                    if (output == null) throw new IOException("Cannot open Downloads ZIP output.");
                    files.CallStatic<long>("copy", source, output); output.Call("flush"); output.Call("close");
                }
                using (var values = new AndroidJavaObject("android.content.ContentValues"))
                using (var zero = new AndroidJavaObject("java.lang.Integer", 0)) {
                    values.Call("put", "is_pending", zero); resolver.Call<int>("update", uri, values, null, null);
                }
                using (var environment = new AndroidJavaClass("android.os.Environment"))
                using (var directory = environment.CallStatic<AndroidJavaObject>("getExternalStoragePublicDirectory", "Download"))
                    publicPath = Path.Combine(directory.Call<string>("getAbsolutePath"), "HumanVisionLogs", Path.GetFileName(zip));
                complete = true; return true;
            } catch (Exception e) { error = DeviceDiagnosticSession.Redact(e.ToString()); return false; }
            finally {
                // 失败的 pending 文件不留在公共 Downloads，应用目录里的原 ZIP 始终保留。
                if (!complete && resolver != null && uri != null) { try { resolver.Call<int>("delete", uri, null, null); } catch { } }
                uri?.Dispose(); resolver?.Dispose();
            }
#else
            error = "Public Downloads export is used on an Android device only."; return false;
#endif
        }
    }
}

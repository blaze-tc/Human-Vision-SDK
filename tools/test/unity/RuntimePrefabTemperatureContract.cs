using System;
using System.IO;
using System.Reflection;
using HumanVisionAcceptance.Diagnostics;

internal static class RuntimePrefabTemperatureContract
{
    private static int failures;

    private static int Main()
    {
        Check("unknown readings are explicitly unavailable", () => {
            const string expected = "温度  CPU 不可读 · GPU 不可读 · NPU 不可读 · 电池 不可读";
            Equal(expected, Format(null));
            Equal(expected, Format(new HardwareSample {
                cpuTemperatureC = double.NaN,
                gpuTemperatureC = double.PositiveInfinity,
                npuTemperatureC = -1,
                batteryTemperatureC = 201
            }));
        });
        Check("component values and battery are reported independently", () => Equal(
            "温度  CPU 63.2°C · GPU 58.0°C · NPU 49.8°C · 电池 32.5°C",
            Format(new HardwareSample {
                cpuTemperatureC = 63.2,
                gpuTemperatureC = 58,
                npuTemperatureC = 49.8,
                batteryTemperatureC = 32.5
            })));
        Check("SoC temperature is never reported as CPU", () => {
            var sample = Capture(("soc-thermal", "68000"));
            Equal(-1d, sample.cpuTemperatureC);
            Equal("温度  CPU 不可读 · GPU 不可读 · NPU 不可读 · 电池 不可读 · SoC 68.0°C", Format(sample));
        });
        Check("separate Rockchip zones retain individual readings and known extra hardware", () => {
            var sample = Capture(
                ("bigcore0-thermal", "60900"),
                ("littlecore-thermal", "56500"),
                ("mali-thermal", "57000"),
                ("gpu-thermal", "59000"),
                ("rknpu-thermal", "55500"),
                ("soc-thermal", "68500"),
                ("ddr-thermal", "50800"),
                ("pmic-thermal", "45400"),
                ("cpu-gpu-shared-thermal", "89000"),
                ("unlabeled-junction", "92000"));
            Equal(60.9d, sample.cpuTemperatureC);
            Equal(59d, sample.gpuTemperatureC);
            Equal(55.5d, sample.npuTemperatureC);
            Equal("温度  CPU 60.9°C · GPU 59.0°C · NPU 55.5°C · 电池 不可读 · SoC 68.5°C · DDR 50.8°C · PMIC 45.4°C",
                Format(sample));
        });
        Check("invalid thermal sysfs readings do not become numbers", () => {
            var sample = Capture(("gpu-thermal", "nan"), ("npu-thermal", "2147483647"), ("ddr-thermal", "bad"));
            Equal("温度  CPU 不可读 · GPU 不可读 · NPU 不可读 · 电池 不可读", Format(sample));
        });
        Check("screen and unrelated socket labels are not misassigned", () => {
            var sample = Capture(("display-therm", "46000"), ("socket-zone", "85000"));
            Equal("温度  CPU 不可读 · GPU 不可读 · NPU 不可读 · 电池 不可读 · 屏幕 46.0°C", Format(sample));
        });
        Check("battery broadcast takes precedence over the battery thermal-zone fallback", () => {
            var sample = Capture(("battery-thermal", "37100"));
            Equal("温度  CPU 不可读 · GPU 不可读 · NPU 不可读 · 电池 37.1°C", Format(sample));
            sample.batteryTemperatureC = 35.2;
            Equal("温度  CPU 不可读 · GPU 不可读 · NPU 不可读 · 电池 35.2°C", Format(sample));
        });
        return failures == 0 ? 0 : 1;
    }

    private static string Format(HardwareSample sample)
    {
        var type = typeof(HardwareSample).Assembly.GetType("HumanVisionAcceptance.Diagnostics.RuntimePrefabTemperatureLine");
        var method = type?.GetMethod("Format", BindingFlags.NonPublic | BindingFlags.Static);
        if (method == null) throw new InvalidOperationException("Temperature footer formatter is missing.");
        return (string)method.Invoke(null, new object[] { sample });
    }

    private static HardwareSample Capture(params (string Type, string Millidegrees)[] sensors)
    {
        string root = Path.Combine(Path.GetTempPath(), "hv-thermal-contract-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try {
            for (int i = 0; i < sensors.Length; i++) {
                string zone = Path.Combine(root, "thermal_zone" + i);
                Directory.CreateDirectory(zone);
                File.WriteAllText(Path.Combine(zone, "type"), sensors[i].Type);
                File.WriteAllText(Path.Combine(zone, "temp"), sensors[i].Millidegrees);
            }
            var sample = new HardwareSample();
            var missing = Path.Combine(root, "missing");
            new DeviceSysfsTelemetry(missing, root, missing, missing, missing).Capture(sample);
            return sample;
        }
        finally {
            foreach (string zone in Directory.GetDirectories(root)) {
                File.Delete(Path.Combine(zone, "type"));
                File.Delete(Path.Combine(zone, "temp"));
                Directory.Delete(zone);
            }
            Directory.Delete(root);
        }
    }

    private static void Equal<T>(T expected, T actual)
    {
        if (!Equals(expected, actual))
            throw new InvalidOperationException("Expected " + expected + ", got " + actual);
    }

    private static void Check(string name, Action test)
    {
        try {
            test();
            Console.WriteLine("PASS " + name);
        }
        catch (Exception error) {
            failures++;
            Console.WriteLine("FAIL " + name + ": " + (error.InnerException ?? error).Message);
        }
    }
}

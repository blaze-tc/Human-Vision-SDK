using System;
using System.Globalization;
using System.Text;

namespace HumanVisionAcceptance.Diagnostics
{
    internal static class RuntimePrefabTemperatureLine
    {
        internal enum SensorKind { Unknown, Cpu, Gpu, Npu, Battery, Soc, Ddr, Pmic, Board, Wifi, Storage, Vpu, Isp, Display, Count }

        internal const string Unavailable = "温度  CPU 不可读 · GPU 不可读 · NPU 不可读 · 电池 不可读";
        private static readonly (SensorKind Kind, string Label)[] ExtraSensors = {
            (SensorKind.Soc, "SoC"), (SensorKind.Ddr, "DDR"), (SensorKind.Pmic, "PMIC"),
            (SensorKind.Board, "主板"), (SensorKind.Wifi, "Wi-Fi"), (SensorKind.Storage, "存储"),
            (SensorKind.Vpu, "VPU"), (SensorKind.Isp, "ISP"), (SensorKind.Display, "屏幕")
        };

        internal static SensorKind Identify(string type)
        {
            string name = (type ?? "").ToLowerInvariant();
            bool cpu = name.Contains("cpu") || name.Contains("bigcore") || name.Contains("littlecore");
            bool gpu = name.Contains("gpu") || name.Contains("mali") || name.Contains("adreno") || name.Contains("kgsl");
            bool npu = name.Contains("npu");
            // Shared domains are kept in the raw sample, without attributing one reading to two chips.
            if (name.Contains("shared") || cpu && (gpu || npu) || gpu && npu) return SensorKind.Unknown;
            if (cpu) return SensorKind.Cpu;
            if (gpu) return SensorKind.Gpu;
            if (npu) return SensorKind.Npu;
            if (name.Contains("battery") || name.StartsWith("batt", StringComparison.Ordinal)) return SensorKind.Battery;
            if (name == "soc" || name.StartsWith("soc-", StringComparison.Ordinal) || name.StartsWith("soc_", StringComparison.Ordinal))
                return SensorKind.Soc;
            if (name.Contains("ddr") || name.Contains("dram")) return SensorKind.Ddr;
            if (name.Contains("pmic")) return SensorKind.Pmic;
            if (name.Contains("board") || name.Contains("pcb")) return SensorKind.Board;
            if (name.Contains("wifi") || name.Contains("wlan")) return SensorKind.Wifi;
            if (name.Contains("nvme") || name.Contains("ufs") || name.Contains("emmc")) return SensorKind.Storage;
            if (name.Contains("vpu")) return SensorKind.Vpu;
            if (name.Contains("display") || name.Contains("panel") || name.Contains("oled")) return SensorKind.Display;
            if (name.StartsWith("isp", StringComparison.Ordinal) || name.Contains("-isp") || name.Contains("_isp"))
                return SensorKind.Isp;
            return SensorKind.Unknown;
        }

        internal static string Format(HardwareSample sample)
        {
            if (sample == null) return Unavailable;
            var temperatures = new double[(int)SensorKind.Count];
            for (int i = 0; i < temperatures.Length; i++) temperatures[i] = -1;
            if (sample.thermalSensors != null) {
                foreach (var sensor in sample.thermalSensors) {
                    if (sensor == null || !IsReadable(sensor.temperatureC)) continue;
                    var kind = Identify(sensor.type);
                    if (kind != SensorKind.Unknown)
                        temperatures[(int)kind] = Math.Max(temperatures[(int)kind], sensor.temperatureC);
                }
            }

            double battery = IsReadable(sample.batteryTemperatureC)
                ? sample.batteryTemperatureC : temperatures[(int)SensorKind.Battery];
            var line = new StringBuilder("温度  ");
            Append(line, "CPU", sample.cpuTemperatureC);
            Append(line.Append(" · "), "GPU", sample.gpuTemperatureC);
            Append(line.Append(" · "), "NPU", sample.npuTemperatureC);
            Append(line.Append(" · "), "电池", battery);
            foreach (var sensor in ExtraSensors) {
                double reading = temperatures[(int)sensor.Kind];
                if (IsReadable(reading)) Append(line.Append(" · "), sensor.Label, reading);
            }
            return line.ToString();
        }

        private static bool IsReadable(double temperature) => temperature >= 0 && temperature <= 200;

        private static void Append(StringBuilder line, string name, double temperature)
        {
            line.Append(name).Append(' ');
            if (IsReadable(temperature))
                line.Append(temperature.ToString("F1", CultureInfo.InvariantCulture)).Append("°C");
            else
                line.Append("不可读");
        }
    }
}

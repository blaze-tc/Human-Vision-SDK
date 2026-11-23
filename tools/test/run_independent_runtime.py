"""Run the independent Android acceptance APK against an owned loopback RTSP fixture."""

import argparse
from contextlib import ExitStack, contextmanager
from datetime import datetime, timezone
import hashlib
import json
from math import isfinite
from pathlib import Path
import re
import subprocess
import sys
import time
from zipfile import BadZipFile, ZipFile

sys.dont_write_bytecode = True

from analyze_independent_runtime import analyze_run

PACKAGE = "com.blazetc.humanvision.runtimeacceptance.independent"
ACTIVITY = "com.unity3d.player.UnityPlayerActivity"
FIXTURE_PATH = "Exercise-11.mp4"
DIAGNOSTICS_MARKER = "HV_RT_DIAGNOSTICS session="
DIAGNOSTICS_PATH = re.compile(
    rf"/(?:storage/emulated/0|sdcard)/Android/data/{re.escape(PACKAGE)}"
    r"/files/RuntimePrefabDiagnostics/runtime-prefab-[0-9-]+\.jsonl"
)
CREATE_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def sha256(stream):
    digest = hashlib.sha256()
    for block in iter(lambda: stream.read(2**20), b""):
        digest.update(block)
    return digest.hexdigest()


def file_sha(path):
    with path.open("rb") as stream:
        return sha256(stream)


def describe_apk(path):
    receipt = {"path": str(path), "sha256": file_sha(path), "native_libraries": {}, "model_assets": {}}
    with ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise RuntimeError("APK has duplicate archive entries")
        for entry in sorted(names):
            parts = entry.lower().split("/")
            if entry.startswith("lib/") and entry.endswith(".so"):
                field = "native_libraries"
            elif entry.startswith("assets/") and not entry.endswith("/") and (
                set(parts).intersection(("modelpacks", "model_packs", "models", "profiles"))
                or Path(entry).suffix.lower() in (".onnx", ".rknn", ".param", ".tflite", ".mnn")
            ):
                field = "model_assets"
            else:
                continue
            with archive.open(entry) as stream:
                receipt[field][entry] = sha256(stream)
    return receipt


def validation_match(pattern, output, label, expected):
    match = re.search(pattern, output, re.MULTILINE)
    if match is None or match[1] != expected:
        got = match[1] if match else "missing"
        raise RuntimeError(f"APK {label} is {got!r}; expected {expected!r}")


def validate_apk_badging(output):
    validation_match(r"^package: name='([^']+)'", output, "package", PACKAGE)
    validation_match(r"^launchable-activity: name='([^']+)'", output, "activity", ACTIVITY)


def diagnostics_path(line):
    if DIAGNOSTICS_MARKER not in line:
        return None
    path = line.split(DIAGNOSTICS_MARKER, 1)[1].strip()
    if DIAGNOSTICS_PATH.fullmatch(path) is None:
        raise RuntimeError(f"Invalid diagnostics path for {PACKAGE}: {path!r}")
    return path


def reverse_mappings(output):
    mappings = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) != 3:
            if fields:
                raise RuntimeError(f"Unexpected adb reverse --list line: {line!r}")
            continue
        _, remote, local = fields
        if remote in mappings:
            raise RuntimeError(f"Ambiguous adb reverse port {remote}")
        mappings[remote] = local
    return mappings


@contextmanager
def owned_reverse(adb, port):
    endpoint = f"tcp:{port}"
    if endpoint in reverse_mappings(adb("reverse", "--list")):
        raise RuntimeError(f"adb reverse {endpoint} already exists")
    # adb makes acquisition atomic even if another run races the list call.
    adb("reverse", "--no-rebind", endpoint, endpoint)
    try:
        yield
    finally:
        if reverse_mappings(adb("reverse", "--list")).get(endpoint) == endpoint:
            adb("reverse", "--remove", endpoint)


def command(args, *, timeout=60, check=True, cwd=None):
    result = subprocess.run(args, timeout=timeout, check=False, cwd=cwd,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=CREATE_NO_WINDOW)
    out = result.stdout.decode("utf-8", errors="replace")
    if check and result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip() or out.strip()
        raise RuntimeError(f"{Path(args[0]).name} {' '.join(map(str, args[1:3]))} failed ({result.returncode}): {detail}")
    return out


@contextmanager
def owned_process(args, log, cwd):
    with log.open("wb") as stream:
        process = subprocess.Popen(args, stdout=stream, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                   cwd=cwd, creationflags=CREATE_NO_WINDOW)
        try:
            yield process
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=8)


def verify_alive(processes):
    for name, process in processes.items():
        if process.poll() is not None:
            raise RuntimeError(f"{name} exited ({process.returncode}); inspect its run log")


def wait_in_log(path, expected, processes, timeout):
    deadline = time.monotonic() + timeout
    with path.open("rb") as stream:
        pending = b""
        while time.monotonic() < deadline:
            verify_alive(processes)
            pending += stream.read(65536)
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                if expected in line:
                    return
            time.sleep(0.2)
    raise RuntimeError(f"Timed out waiting for {expected.decode()} in {path.name}")


def save_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def hardware_sample(adb, args, stage):
    battery = adb("shell", "dumpsys", "battery")
    thermal = adb("shell", "dumpsys", "thermalservice")
    # Some Android systems restrict frequency sysfs nodes. Empty output is unknown.
    cpu_frequency = adb("shell", "cat", "/sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq", check=False)
    gpu_frequency = adb("shell", "cat", "/sys/class/kgsl/kgsl-3d0/gpuclk", check=False)
    temp_match = re.search(r"^\s*temperature:\s*(-?\d+)\s*$", battery, re.MULTILINE)
    status_match = re.search(r"^\s*Thermal Status:\s*(\d+)\s*$", thermal, re.MULTILINE)
    row = {
        "utc": utc_now(), "stage": stage,
        "battery_temperature_c": int(temp_match[1]) / 10 if temp_match else None,
        "android_thermal_status": int(status_match[1]) if status_match else None,
        "cpu_policy_frequency_khz": [int(item) for item in cpu_frequency.split() if item.isdecimal()] or None,
        "gpu_frequency_hz": int(gpu_frequency.strip()) if gpu_frequency.strip().isdecimal() else None,
        "battery": battery, "thermalservice": thermal,
    }
    with (args.output / "thermal.jsonl").open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(row, ensure_ascii=False, allow_nan=False) + "\n")
    if args.max_battery_c is not None:
        temperature = row["battery_temperature_c"]
        if temperature is None or temperature > args.max_battery_c:
            raise RuntimeError(f"Battery temperature {temperature} C is unavailable or above {args.max_battery_c} C")
    return {key: value for key, value in row.items() if key not in ("battery", "thermalservice")}


def launch_extras(args, fixture_url):
    return [
        "--es", "hv_acceptance_fixture", fixture_url,
        "--ei", "hv_acceptance_stage_seconds", str(args.seconds + args.warmup),
        "--ei", "hv_acceptance_target_fps", str(args.target_fps),
        "--ez", "hv_acceptance_trace_cpu", str(args.backend == "cpu").lower(),
        "--es", "hv_independent_execution", args.execution,
        "--ez", "hv_independent_preview_off", str(args.preview_off).lower(),
        "--ei", "hv_independent_stall_ms", str(args.stall_ms or 0),
        "--ei", "hv_independent_stall_after_s", str(args.stall_after_s),
    ]


def wait_for_ready(adb, logcat, processes, not_before, timeout, receipt):
    deadline = time.monotonic() + timeout
    path = None
    with logcat.open(encoding="utf-8", errors="replace") as stream:
        while time.monotonic() < deadline:
            verify_alive(processes)
            if path is None:
                while True:
                    offset = stream.tell()
                    line = stream.readline()
                    if not line.endswith("\n"):
                        stream.seek(offset)
                        break
                    match = re.match(r"^\s*(\d{9,}\.\d+)\s", line)
                    if match and float(match[1]) >= not_before and DIAGNOSTICS_MARKER in line:
                        path = diagnostics_path(line)
                        receipt["diagnostics_device_path"] = path
                        break
            if path:
                tail = adb("exec-out", "tail", "-n", "60", path, check=False, timeout=12)
                for line in tail.splitlines():
                    try:
                        record = json.loads(line)
                    except ValueError:
                        continue
                    if isinstance(record, dict) and record.get("kind") == "mark" and record.get("outcome") == "ready":
                        return record
            time.sleep(0.5)
    raise RuntimeError("Acceptance APK did not become ready; inspect launch.txt and logcat.txt")


def install_apk(adb, args, receipt):
    install_log = adb("install", "-r", "--no-streaming", args.apk, timeout=180)
    (args.output / "install.txt").write_text(install_log, encoding="utf-8")
    installed = adb("shell", "pm", "path", PACKAGE).strip()
    match = re.fullmatch(r"package:(/data/app/[A-Za-z0-9_/.+~=-]+/base\.apk)", installed)
    if match is None:
        raise RuntimeError(f"Expected one installed base APK for {PACKAGE}: {installed!r}")
    output = adb("shell", "sha256sum", match[1]).split()
    installed_sha = output[0] if output else None
    receipt["installed_apk"] = {"path": match[1], "sha256": installed_sha}
    if installed_sha != receipt["apk"]["sha256"]:
        raise RuntimeError("Installed APK SHA-256 differs from the selected APK")


def run_capture(adb, args, receipt):
    output = args.output
    url = f"rtsp://127.0.0.1:{args.port}/{FIXTURE_PATH}"
    config = output / "mediamtx.yml"
    config.write_text(
        f"rtspAddress: 127.0.0.1:{args.port}\nrtspTransports: [tcp]\n"
        "rtmp: no\nhls: no\nwebrtc: no\nsrt: no\npaths:\n  all_others:\n",
        encoding="utf-8",
    )
    logcat = output / "logcat.txt"
    diagnostics = output / "runtime-prefab.jsonl"
    with ExitStack() as stack:
        processes = {}
        server_log = output / "server.log"
        processes["MediaMTX"] = stack.enter_context(owned_process([args.mediamtx, config], server_log, output))
        wait_in_log(server_log, f"listener opened on 127.0.0.1:{args.port} (TCP)".encode(), processes, 15)
        stack.enter_context(owned_reverse(adb, args.port))
        processes["RTSP publisher"] = stack.enter_context(owned_process([
            args.ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "warning",
            "-stream_loop", "-1", "-re", "-i", args.source, "-map", "0:v:0", "-an", "-c:v", "copy",
            "-f", "rtsp", "-rtsp_transport", "tcp", url,
        ], output / "publisher.log", output))
        wait_in_log(server_log, f"is publishing to path '{FIXTURE_PATH}'".encode(), processes, 15)
        receipt["thermal_start"] = hardware_sample(adb, args, "start")
        install_apk(adb, args, receipt)
        adb("shell", "am", "force-stop", PACKAGE)
        adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
        adb("shell", "wm", "dismiss-keyguard", check=False)
        # Guard against an old logcat record if Android reuses a process ID.
        device_epoch_before = int(adb("shell", "date", "+%s").strip())
        receipt["launch"] = {"host_utc": utc_now(), "device_epoch_before": device_epoch_before,
                             "activity": f"{PACKAGE}/{ACTIVITY}", "extras": launch_extras(args, url)}
        save_json(output / "receipt.json", receipt)
        stack.callback(adb, "shell", "am", "force-stop", PACKAGE)
        launch = adb("shell", "am", "start", "-W", "-n", f"{PACKAGE}/{ACTIVITY}", *receipt["launch"]["extras"])
        (output / "launch.txt").write_text(launch, encoding="utf-8")
        if not re.search(r"^Status:\s+ok\s*$", launch, re.MULTILINE):
            raise RuntimeError("Android did not confirm activity launch; inspect launch.txt")
        pid = adb("shell", "pidof", PACKAGE).strip()
        if re.fullmatch(r"[1-9]\d*", pid) is None:
            raise RuntimeError(f"Expected one APK process, received pid {pid!r}")
        receipt["pid"] = int(pid)
        print(f"Recording {PACKAGE} pid={pid} in {output}", flush=True)
        processes["adb logcat"] = stack.enter_context(owned_process(
            [args.adb, "-s", args.serial, "logcat", "-v", "epoch", f"--pid={pid}"], logcat, output,
        ))
        try:
            receipt["ready_marker"] = wait_for_ready(
                adb, logcat, processes, device_epoch_before, args.ready_timeout, receipt,
            )
            receipt["ready_observed_utc"] = utc_now()
            save_json(output / "receipt.json", receipt)
            deadline = time.monotonic() + args.warmup + args.seconds + 2
            next_thermal = time.monotonic() + args.thermal_interval
            while time.monotonic() < deadline:
                verify_alive(processes)
                if time.monotonic() >= next_thermal:
                    hardware_sample(adb, args, "running")
                    next_thermal = time.monotonic() + args.thermal_interval
                time.sleep(min(0.5, max(0, deadline - time.monotonic())))
        finally:
            try:
                if path := receipt.get("diagnostics_device_path"):
                    adb("pull", path, diagnostics)
            finally:
                receipt["thermal_end"] = hardware_sample(adb, args, "end")
    return analyze_run(
        logcat, diagnostics, args.warmup, args.seconds, receipt["pid"], capture_start_epoch=device_epoch_before,
        requested_stall_ms=args.stall_ms, expected_backend=args.backend, expected_execution=args.execution,
        expected_preview=False if args.preview_off else None, expected_target_fps=args.target_fps,
    )


def prepare_args(parser):
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--output", type=Path, required=True, help="New evidence directory.")
    parser.add_argument("--backend", choices=("cpu", "gpu"), required=True)
    parser.add_argument("--seconds", type=int, required=True, help="Measurement duration after readiness and warm-up.")
    parser.add_argument("--warmup", type=int, default=4)
    parser.add_argument("--source", type=Path, required=True, help="Local H.264 MP4 fixture.")
    parser.add_argument("--mediamtx", type=Path, required=True)
    parser.add_argument("--ffmpeg", type=Path, required=True)
    parser.add_argument("--adb", type=Path, required=True)
    parser.add_argument("--aapt", type=Path, help="Defaults to an installed build-tools alongside adb.")
    parser.add_argument("--port", type=int, default=18569)
    parser.add_argument("--execution", choices=("compatible", "native"), default="compatible")
    parser.add_argument("--preview-off", action="store_true")
    parser.add_argument("--target-fps", type=int, choices=(15, 30, 60), default=30)
    parser.add_argument("--stall-ms", type=int, choices=(500, 2000))
    parser.add_argument("--stall-after-s", type=int, default=8)
    parser.add_argument("--ready-timeout", type=int, default=65)
    parser.add_argument("--thermal-interval", type=int, default=60)
    parser.add_argument("--max-battery-c", type=float)
    parser.add_argument("--build-commit", help="Commit used to build the selected APK, if verified.")
    args = parser.parse_args()
    if args.seconds <= 0 or args.warmup < 0 or args.seconds + args.warmup < 9:
        parser.error("seconds must be positive, warmup nonnegative, and the combined stage at least 9 seconds")
    if not 1024 <= args.port <= 65535:
        parser.error("port must be in 1024..65535")
    if min(args.ready_timeout, args.thermal_interval) <= 0:
        parser.error("ready timeout and thermal interval must be positive")
    if args.stall_ms and (args.stall_after_s < 0 or args.stall_after_s + args.stall_ms / 1000 >= args.seconds + args.warmup):
        parser.error("the requested stall must fit inside the stage")
    if args.max_battery_c is not None and not isfinite(args.max_battery_c):
        parser.error("battery ceiling must be finite")
    if not re.fullmatch(r"[A-Za-z0-9_.:-]+", args.serial):
        parser.error("invalid adb serial")
    if args.build_commit is not None and not re.fullmatch(r"(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})", args.build_commit):
        parser.error("build commit must be a full Git commit hash")
    if args.aapt is None:
        name = "aapt.exe" if sys.platform == "win32" else "aapt"
        choices = list(args.adb.resolve().parent.parent.glob(f"build-tools/*/{name}"))
        if not choices:
            parser.error("supply --aapt to verify APK identity before installation")
        args.aapt = max(choices, key=lambda path: tuple(map(int, re.findall(r"\d+", path.parent.name))))
    for field in ("apk", "source", "mediamtx", "ffmpeg", "adb", "aapt"):
        path = getattr(args, field).resolve()
        if not path.is_file():
            parser.error(f"--{field} must point to an existing file: {path}")
        setattr(args, field, path)
    args.output = args.output.resolve()
    return args


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    args = prepare_args(parser)
    try:
        args.output.mkdir(parents=True)
    except OSError as error:
        parser.exit(1, f"{error}\n")

    def adb(*argv, **options):
        return command([args.adb, "-s", args.serial, *argv], cwd=args.output, **options)

    receipt = {
        "schema_version": 1, "status": "running", "started_utc": utc_now(), "package": PACKAGE,
        "serial": args.serial, "build_commit": args.build_commit, "reverse_port": args.port,
        "requested": {"backend": args.backend, "execution_mode": args.execution, "target_fps": args.target_fps,
                      "preview_off": args.preview_off, "stall_ms": args.stall_ms, "stall_after_s": args.stall_after_s,
                      "warmup_seconds": args.warmup, "measurement_seconds": args.seconds,
                      "max_battery_c": args.max_battery_c},
    }
    try:
        badging = command([args.aapt, "dump", "badging", args.apk], cwd=args.output)
        (args.output / "apk-badging.txt").write_text(badging, encoding="utf-8")
        validate_apk_badging(badging)
        receipt["apk"] = describe_apk(args.apk)
        receipt["source"] = {"path": str(args.source), "sha256": file_sha(args.source)}
        receipt["device"] = {
            name: adb("shell", "getprop", prop).strip()
            for name, prop in (("model", "ro.product.model"), ("android", "ro.build.version.release"),
                               ("fingerprint", "ro.build.fingerprint"))
        }
        save_json(args.output / "receipt.json", receipt)
        report = run_capture(adb, args, receipt)
        save_json(args.output / "analysis.json", report)
        receipt["status"] = "captured"
        receipt["analysis_status"] = report["status"]
        print(f"Stored capture and analysis ({report['status']}) in {args.output}", flush=True)
        return 2 if report["status"] == "invalid" else 0
    except (OSError, ValueError, RuntimeError, BadZipFile, subprocess.TimeoutExpired, KeyboardInterrupt) as error:
        receipt["status"] = "interrupted" if isinstance(error, KeyboardInterrupt) else "failed"
        receipt["error"] = str(error) or type(error).__name__
        print(receipt["error"], file=sys.stderr)
        return 1
    finally:
        receipt["finished_utc"] = utc_now()
        save_json(args.output / "receipt.json", receipt)


if __name__ == "__main__":
    raise SystemExit(main())

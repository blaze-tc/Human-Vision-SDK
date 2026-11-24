"""Compile/run the actual probe's pure parameter contract; never control Unity/ADB."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--unity-data", type=Path, required=True)
    parser.add_argument("--sdk-assemblies", type=Path, required=True,
                        help="Verified matching HumanVision.Input/Runtime/Demo DLL directory.")
    parser.add_argument("--output", type=Path, required=True, help="New standalone output directory.")
    parser.add_argument("--define", action="append", default=[], help="C# conditional symbol, e.g. UNITY_EDITOR or UNITY_ANDROID.")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error("Output directory already exists; preserve earlier results.")
    mono = args.unity_data / "MonoBleedingEdge/bin/mono.exe"
    csc = args.unity_data / "MonoBleedingEdge/lib/mono/msbuild/Current/bin/Roslyn/csc.exe"
    source = Path(__file__).resolve().parent / "unity"
    inputs = [source / "RuntimeIndependentAcceptanceProbe.cs", source / "RuntimeNpuStartupOptionsContract.cs"]
    references = sorted((args.unity_data / "Managed/UnityEngine").glob("*.dll"))
    references += [args.sdk_assemblies / ("HumanVision." + name + ".dll") for name in ("Input", "Runtime", "Demo")]
    for path in [mono, csc, *inputs, *references]:
        if not path.is_file():
            parser.error("Missing compile input: " + str(path))
    output.mkdir(parents=True)
    executable = output / "npu-startup-contract.exe"
    command = [str(mono), str(csc), "-nologo", "-langversion:9.0", "-out:" + str(executable),
               *["-define:" + value for value in args.define],
               *map(str, inputs), *["-r:" + str(path) for path in references]]
    compile_result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (output / "compile.log").write_bytes(compile_result.stdout)
    receipt = {"scope": "pure parameters/JSON only; no Unity or hardware execution",
               "defines": args.define,
               "inputs": {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in [*inputs, *references]}, "compile_exit": compile_result.returncode}
    if compile_result.returncode == 0:
        for path in references:
            shutil.copyfile(path, output / path.name)
        run = subprocess.run([str(mono), str(executable)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (output / "run.log").write_bytes(run.stdout)
        receipt["run_exit"] = run.returncode
        print(run.stdout.decode("utf-8-sig", errors="replace"))
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return receipt.get("run_exit", receipt["compile_exit"] or 1)


if __name__ == "__main__":
    raise SystemExit(main())

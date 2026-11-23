"""Run console unit contracts for the real private Demo and SDK query sources.

Unity I/O/Time is controlled by the harness; results are not device acceptance.
The source is copied into the output directory, never edited or staged to Unity.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--unity-editor", type=Path, default=Path("D:/Developer/2021.3.45f1/Editor"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    original = args.source.read_bytes()
    text = original.decode("utf-8-sig")
    marker = "using UnityEngine;"
    if text.count(marker) != 1:
        parser.error("Expected one UnityEngine import in the actual LiveSkeleton source")
    under_test = output / "RuntimePrefabLiveSkeleton.UnderTest.cs"
    under_test.write_text(text.replace(marker, marker + "\nusing Time = LiveSkeletonContract.Clock;"), encoding="utf-8")
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    properties = ET.SubElement(project, "PropertyGroup")
    for name, value in {"OutputType": "Exe", "TargetFramework": "net8.0", "EnableDefaultCompileItems": "false", "Nullable": "disable", "NuGetAudit": "false"}.items():
        ET.SubElement(properties, name).text = value
    items = ET.SubElement(project, "ItemGroup")
    runtime = root / "upm/com.blazetc.humanvision/Runtime"
    sources = [under_test, root / "tools/test/unity/RuntimePrefabLiveSkeletonContract.cs",
        runtime / "HumanVisionBody.cs", runtime / "HumanVisionCanonicalJoint.cs",
        runtime / "HumanVisionSdkConfiguration.cs", runtime / "HumanVisionSkeletonQueries.cs",
        runtime / "HumanVisionRuntimeProgress.cs",
        root / "upm/com.blazetc.humanvision/Samples~/RuntimePrefab/RuntimeSkeletonReader.cs"]
    for source in sources:
        ET.SubElement(items, "Compile", Include=str(source.resolve()))
    for name in ("CoreModule", "IMGUIModule"):
        path = args.unity_editor / f"Data/Managed/UnityEngine/UnityEngine.{name}.dll"
        if not path.is_file():
            parser.error(f"Pinned Unity reference is missing: {path}")
        reference = ET.SubElement(items, "Reference", Include=f"UnityEngine.{name}")
        ET.SubElement(reference, "HintPath").text = str(path.resolve())
    project_path = output / "LiveSkeletonContract.csproj"
    ET.ElementTree(project).write(project_path, encoding="utf-8", xml_declaration=True)
    receipt = {"source": str(args.source.resolve()), "source_sha256": hashlib.sha256(original).hexdigest(),
        "scope": "controlled console unit test, not device acceptance", "files": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
    for name, command in [
        ("restore", ["dotnet", "restore", str(project_path), "--ignore-failed-sources", "--verbosity", "quiet"]),
        ("build", ["dotnet", "build", str(project_path), "--no-restore", "--verbosity", "quiet", "-m:1"]),
        ("run", ["dotnet", str(output / "bin/Debug/net8.0/LiveSkeletonContract.dll")])]:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", cwd=root)
        (output / (name + ".log")).write_text(result.stdout, encoding="utf-8")
        receipt[name + "_exit"] = result.returncode
        print(name + ": " + str(result.returncode))
        if name == "run" or result.returncode:
            print(result.stdout)
        if result.returncode:
            (output / "receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
            return result.returncode
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

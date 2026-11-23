"""Check real source-count deltas separately from main-thread observations."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--legacy',action='store_true')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=root/('upm/com.blazetc.humanvision/Runtime/Demo/Sdk/HumanVisionObservedRate.cs' if args.legacy else 'tools/test/unity/interactive/RuntimeCounterRateWindow.cs')
    harness=root/'tools/test/unity/RuntimeCounterRateContract.cs'
    project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');prop=ET.SubElement(project,'PropertyGroup')
    for k,v in {'OutputType':'Exe','TargetFramework':'net8.0','EnableDefaultCompileItems':'false','NuGetAudit':'false','DefineConstants':'LEGACY_OBSERVED_RATE' if args.legacy else ''}.items():ET.SubElement(prop,k).text=v
    items=ET.SubElement(project,'ItemGroup')
    for file in [source,harness]:ET.SubElement(items,'Compile',Include=str(file))
    path=out/'CounterRate.csproj';ET.ElementTree(project).write(path,encoding='utf-8',xml_declaration=True)
    receipt={'legacy':args.legacy,'scope':'pure counter-rate unit contract; not hardware acceptance','sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [source,harness]}}
    for name,cmd in [('restore',['dotnet','restore',str(path),'--ignore-failed-sources','--verbosity','quiet']),('build',['dotnet','build',str(path),'--no-restore','--verbosity','quiet','-m:1']),('run',['dotnet',str(out/'bin/Debug/net8.0/CounterRate.dll')])]:
        r=subprocess.run(cmd,capture_output=True,text=True,encoding='utf-8',errors='replace');(out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');receipt[name+'_exit']=r.returncode
        print(name,r.returncode)
        if name=='run':print(r.stdout)
        if r.returncode:break
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
    return r.returncode

if __name__=='__main__':raise SystemExit(main())

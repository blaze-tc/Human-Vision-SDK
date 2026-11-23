"""Run sample preview composition with controlled draw boundaries, not Unity/device acceptance."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

def method(text, signature):
    start=text.find("        private "+signature)
    if start<0:return ""
    opening=text.index("{",start);depth=1;end=opening+1
    while depth:
        depth+=(text[end]=="{")-(text[end]=="}");end+=1
    return text[start:end]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--acceptance",type=Path,required=True);p.add_argument("--preview",type=Path,required=True);p.add_argument("--output",type=Path,required=True);p.add_argument("--sidebar",type=Path);a=p.parse_args()
    root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    acceptance=a.acceptance.read_text(encoding="utf-8-sig");preview=a.preview.read_text(encoding="utf-8-sig")
    single=re.search(r"DrawUnifiedPreviewCard\((\w+), (\w+)\);",acceptance)
    if single:
        layout="DrawUnifiedPreviewCard(liveCard, poseCard);"
    else:
        start=acceptance.index("                DrawCard(liveCard,");end=acceptance.index("                DrawFullscreenButton(poseCard);",start)+len("                DrawFullscreenButton(poseCard);");layout=acceptance[start:end]
    unified=method(preview,"void DrawUnifiedPreviewCard(")
    image=method(preview,"void DrawPreviewImage(") or "private void DrawPreviewImage(Rect image,Texture texture,bool showRegions,bool drawSkeleton,float strokeWidth=2) {if(texture!=null)DrawCameraImage(image,texture);}"
    full=method(preview,"void DrawFullscreenPreview(");condition=re.search(r"if \((live != null[^\n]*)\) \{",full).group(1)
    expanded=method(preview,"static void GetExpandedCards(")
    fields="\n".join(re.findall(r"private static readonly Rect (?:LiveCard|PoseCard) = new Rect\([^;]+;",acceptance))
    image_window=re.search(r"private static Rect CardImageWindow\([^;]+;",preview).group(0)
    sidebar_path=a.sidebar or a.preview.parent/"RuntimePrefabAcceptanceSidebar.cs"
    sidebar=method(sidebar_path.read_text(encoding="utf-8-sig"),"void DrawControlSidebar(") if sidebar_path.exists() else ""
    sidebar_call="DrawControlSidebar(new Rect(28,86,480,752));" if sidebar else ""
    wrapper="namespace PreviewContract { internal sealed partial class Demo {"+fields+expanded+image_window+unified+image+sidebar+"public void DrawSidebarFrame(){"+sidebar_call+"}public void DrawLayout(){GetExpandedCards(1,out var liveCard,out var poseCard,out _);SidebarRect=liveCard;"+layout+"} public Rect ImageWindow()=>CardImageWindow(CardRect);public void DrawFullscreenContent(){if("+condition+")DrawPreviewImage(new Rect(0,0,160,90),live,true,showSkeleton);} } }"
    actual=out/"ActualPreviewComposition.cs";actual.write_text(wrapper,encoding="utf-8")
    project=ET.Element("Project",Sdk="Microsoft.NET.Sdk");group=ET.SubElement(project,"PropertyGroup")
    for key,value in {"OutputType":"Exe","TargetFramework":"net8.0","EnableDefaultCompileItems":"false","NuGetAudit":"false"}.items():ET.SubElement(group,key).text=value
    items=ET.SubElement(project,"ItemGroup")
    for source in (actual,root/"tools/test/unity/RuntimePrefabPreviewContract.cs"):ET.SubElement(items,"Compile",Include=str(source))
    project_path=out/"PreviewContract.csproj";ET.ElementTree(project).write(project_path,encoding="utf-8")
    receipt={"scope":"actual sample composition; draw I/O controlled, not device acceptance"}
    for name,command in (("build",["dotnet","build",str(project_path),"--verbosity","quiet","-m:1"]),("run",["dotnet",str(out/"bin/Debug/net8.0/PreviewContract.dll")])):
        result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding="utf-8",errors="replace",cwd=root);(out/(name+".log")).write_text(result.stdout,encoding="utf-8");receipt[name+"_exit"]=result.returncode;print(name+": "+str(result.returncode));print(result.stdout)
        if result.returncode:(out/"receipt.json").write_text(json.dumps(receipt,indent=2),encoding="utf-8");return result.returncode
    (out/"receipt.json").write_text(json.dumps(receipt,indent=2),encoding="utf-8");return 0
if __name__=="__main__":raise SystemExit(main())

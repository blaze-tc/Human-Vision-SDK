from pathlib import Path
import json, re, unicodedata
ROOT = Path(__file__).resolve().parents[2]
GUIDE = ROOT / 'docs/user-guide'
def read(path): return path.read_text(encoding='utf-8-sig')
def anchors(path):
    slugs=set(); seen={}; fenced=False
    for line in read(path).splitlines():
        if line.startswith('```'): fenced=not fenced; continue
        if fenced or not re.match(r'^#{1,6}\s',line): continue
        title=re.sub(r'^#{1,6}\s+','',line).lower().strip()
        title=re.sub(r'[`*]','',title)
        title=''.join(c for c in title if c in '-_ ' or unicodedata.category(c)[0] in 'LN')
        title=title.replace(' ','-')
        n=seen.get(title,0); seen[title]=n+1
        slugs.add(title if n==0 else f'{title}-{n}')
    return slugs

def links():
    paths=list(GUIDE.glob('*.md'))+[ROOT/'README.md',ROOT/'docs/UPM_INSTALLATION.md',ROOT/'docs/maintenance/START_HERE.md',ROOT/'docs/reports/2026-10-07-sdk-user-docs-verification.md',ROOT/'docs/DEVELOPMENT_STATUS.md']
    count=0
    for p in paths:
        # Only new links for existing navigation files, which also carry historical references.
        body=read(p)
        if p.name=='DEVELOPMENT_STATUS.md': body=body.split('The user accepted',1)[0]
        for target in re.findall(r'(?<!!)\[[^\]\n]+\]\(([^)]+)\)',body):
            if re.match(r'https?://',target): continue
            target=target.strip('<>')
            path,_,fragment=target.partition('#')
            q=(p.parent/path).resolve() if path else p
            assert q.exists(), f'Broken link {p.name}: {target}'
            if fragment and q.suffix=='.md':
                assert fragment in anchors(q), f'Broken anchor {p.name}: {target}; choices={anchors(q)}'
            count+=1
    return count

def api_names():
    api=read(GUIDE/'API_REFERENCE.md')
    files=['Runtime/HumanVisionManager.cs','Runtime/HumanVisionConfig.cs',
      'Runtime/HumanVisionBody.cs','Runtime/HumanVisionCanonicalJoint.cs','Runtime/HumanVisionStats.cs',
      'Runtime/HumanVisionRuntimeData.cs','Runtime/HumanVisionException.cs',
      'Runtime/Android/HumanVisionAndroidRuntimeSelection.cs',
      'Runtime/Demo/VideoPlayerFrameSource.cs','Runtime/Demo/HumanVisionOverlay.cs',
      'Runtime/Demo/Input/HumanVisionInputAdapter.cs',
      'Runtime/Demo/Live/HumanVisionCameraManager.cs','Runtime/Demo/Input/AnalysisContract.cs',
      'Runtime/Demo/Input/SharedRecognitionSettings.cs','Runtime/Demo/Input/DemoModeSettings.cs',
      'Runtime/HumanVisionModelInputQualities.cs','Runtime/Demo/Input/HumanVisionSettingsStore.cs']
    names=set()
    for f in files:
        source=read(ROOT/'upm/com.blazetc.humanvision'/f)
        source=re.sub(r'//[^\n]*|/\*.*?\*/','',source,flags=re.S)
        for line in source.splitlines():
            if re.match(r'\s*public\s',line) and not re.search(r'\b(class|enum|struct|interface)\b',line):
                m=re.search(r'\b(\w+)\s*(?:\(|=>|\{|[;=])',line)
                if m: names.add(m.group(1))
    # Nested private serialization helper fields in the quality reader are implementation data.
    names.discard('EditorHostResolver')
    missing=[n for n in sorted(names) if not re.search(r'\b'+re.escape(n)+r'\b',api)]
    assert not missing, f'Public API names not documented: {missing}'
    return len(names)

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description='Check SDK user-guide links and selected public API names.')
    parser.add_argument('--probe-result', type=Path)
    args=parser.parse_args()
    checked=list(GUIDE.rglob('*.md'))+list((GUIDE/'examples').glob('*.cs'))
    forbidden=['Sensory', 'HurdleKing', 'HumanVisionGameRuntime', 'SensoryGame.Vision',
               'PoseMotionDetector', 'VisionSettingsView', '正式项目', 'YS-Sensory']
    for path in checked:
        assert not any(term in read(path) for term in forbidden), f'Application-specific content: {path}'
    result={'local_links_checked':links(),'documented_public_api_names_checked':api_names(),
            'application_specific_content_absent':True,'result':'PASS'}
    if args.probe_result:
        probe=json.loads(read(args.probe_result))
        assert probe['passed'] and probe['unity']=='2021.3.45f1' and not probe['hardware_test']
        project=args.probe_result.parent
        for example in (GUIDE/'examples').glob('*.cs'):
            assert example.read_bytes()==(project/'Assets/Scripts'/example.name).read_bytes()
        result['new_project_probe']=probe
    print(json.dumps(result,ensure_ascii=False))

from pathlib import Path
import json, shutil
import argparse
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description='Create a fresh Unity project for SDK guide verification; never resets existing projects.')
parser.add_argument('--project', type=Path, default=root/'out/sdk-user-guide-new-project')
args=parser.parse_args()
project=args.project.resolve()
assert not project.exists(), 'Use a fresh path; never reset an existing project.'
for directory in ['Assets/Scripts','Assets/Editor','Packages','ProjectSettings']:
    (project/directory).mkdir(parents=True,exist_ok=True)
for source in (root/'docs/user-guide/examples').glob('*.cs'):
    shutil.copyfile(source,project/'Assets/Scripts'/source.name)
dependencies={
 'com.blazetc.humanvision.input':'file:'+ (root/'upm/com.blazetc.humanvision.input').as_posix(),
 'com.blazetc.humanvision':'file:'+ (root/'upm/com.blazetc.humanvision').as_posix(),
 'com.unity.ugui':'1.0.0',
 'com.unity.modules.audio':'1.0.0',
 'com.unity.modules.imageconversion':'1.0.0',
 'com.unity.modules.imgui':'1.0.0',
 'com.unity.modules.jsonserialize':'1.0.0',
 'com.unity.modules.physics':'1.0.0',
 'com.unity.modules.ui':'1.0.0',
 'com.unity.modules.unitywebrequest':'1.0.0',
 'com.unity.modules.video':'1.0.0'}
(project/'Packages/manifest.json').write_text(json.dumps({'dependencies':dependencies},indent=2),encoding='utf-8')
(project/'ProjectSettings/ProjectVersion.txt').write_text('m_EditorVersion: 2021.3.45f1\n',encoding='utf-8')
shutil.copyfile(root/'tools/docs/SdkDocsProbe.cs', project/'Assets/Editor/SdkDocsProbe.cs')
print(project)

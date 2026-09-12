"""Reproduce a package-only Git checkout on Windows with autocrlf enabled."""
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
package=root/'upm/com.blazetc.humanvision'
fixture=Path(tempfile.mkdtemp(prefix='upm-git-newlines-',dir=root/'out'))
index=json.loads((package/'RuntimeData/index.json').read_text(encoding='utf-8'))
entries=[item for item in index['files'] if item['path'].startswith('profiles/')]
for entry in entries:
    target=fixture/'RuntimeData'/entry['path'];target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(package/'RuntimeData'/entry['path'],target)
if (package/'.gitattributes').exists():shutil.copyfile(package/'.gitattributes',fixture/'.gitattributes')
def git(*args):subprocess.run(['git','-C',str(fixture),'-c','core.autocrlf=true',*args],check=True,capture_output=True)
git('init');git('add','.')
export=fixture/'checkout';export.mkdir()
git('checkout-index','--all','--prefix='+export.as_posix()+'/')
for entry in entries:
    data=(export/'RuntimeData'/entry['path']).read_bytes()
    assert hashlib.sha256(data).hexdigest()==entry['sha256'], 'Git autocrlf changed '+entry['path']
print('Package-only Git checkout with autocrlf=true: PASS')

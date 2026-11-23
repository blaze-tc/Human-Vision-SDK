"""Stage SDK-owned managed/shader assets into the acceptance project, preserving scene GUIDs."""
import argparse, hashlib, importlib, json, shutil, sys
from pathlib import Path

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path,required=True)
    parser.add_argument('--project',type=Path,required=True)
    parser.add_argument('--backup',type=Path,required=True)
    parser.add_argument('--receipt',type=Path,required=True)
    parser.add_argument('--apply',action='store_true')
    args=parser.parse_args()
    root=args.source_root.resolve(); project=args.project.resolve()
    sys.path.insert(0,str(root/'tools/package'))
    from package_release_snapshot import offline_assets
    assets,_=offline_assets(root)
    changes=[]
    for rel,(data,meta) in sorted(assets.items()):
        if '/Samples/' in rel: continue  # Acceptance fixture belongs to the test project.
        target=project/rel
        if target.suffix not in ('.cs','.asmdef','.shader'): continue
        if not target.resolve().is_relative_to(project): raise ValueError('Path escaped project')
        before=sha(target); after=hashlib.sha256(data).hexdigest()
        if before==after: continue
        existing_meta=target.with_name(target.name+'.meta')
        if existing_meta.is_file():
            old_guid=next((line for line in existing_meta.read_text(encoding='utf-8-sig').splitlines() if line.startswith('guid:')),None)
            new_guid=next((line for line in meta.decode('utf-8-sig').splitlines() if line.startswith('guid:')),None)
            if old_guid!=new_guid and target.name!='RtspStreamSelector.cs':
                raise ValueError('Unexpected GUID difference: '+rel)
        changes.append((rel,target,data,meta,before,after))
    if args.apply:
        for rel,target,data,meta,before,after in changes:
            backup=args.backup/rel
            if before is not None and not backup.exists():
                backup.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(target,backup)
                existing_meta=target.with_name(target.name+'.meta')
                if existing_meta.exists(): shutil.copy2(existing_meta,backup.with_name(backup.name+'.meta'))
            target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(data)
            existing_meta=target.with_name(target.name+'.meta')
            if not existing_meta.exists(): existing_meta.write_bytes(meta)
    result={'source_root':str(root),'project':str(project),'applied':args.apply,'changed':[{'path':rel,'before':before,'after':after} for rel,_,_,_,before,after in changes]}
    args.receipt.parent.mkdir(parents=True,exist_ok=True)
    args.receipt.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'applied':args.apply,'changed_assets':len(changes),'receipt':str(args.receipt)}))
if __name__=='__main__': main()

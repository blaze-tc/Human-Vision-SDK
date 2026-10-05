"""Root-owned offline Android executions with immutable, fail-closed evidence.

Uploads research fixture tensors only. Never installs APKs or changes runtime.
"""
from pathlib import Path
import argparse
import json
import shutil
import subprocess
import sys
import time
import uuid
sys.path.insert(0,str(Path(__file__).resolve().parent))
from yolo_pose_gate import MODEL_HASHES,sha,compare_directory as base_compare,execution_options
from yolo_rectangle960_prepare import validate_fixture, NAMES

def runner_recipe(mode):
    if mode != "gpu-fp32": raise ValueError("rectangle960 requires gpu-fp32")
    base=Path(__file__).resolve().parent
    return base/"yolo_rectangle960_golden_runner.cpp",base/"yolo_rectangle960_runner/CMakeLists.txt"

def compare_directory(directory,mode="gpu-fp32"):
    try:
        fixture=validate_fixture(directory)
        report=base_compare(directory,mode,runner_recipe_override=runner_recipe(mode))
        if fixture['fixture']=='seven-960' and 'reference' in report and 'actual' in report:
            g=fixture['geometry']
            def raised(bodies):
                count=0
                for body in bodies:
                    shoulder,wrist=body['joints'][5],body['joints'][9]
                    if all(j[2]>=.2 and 0<=j[0]<g['source_width'] and 0<=j[1]<g['source_height']
                           for j in (shoulder,wrist)) and wrist[1]<shoulder[1]:count+=1
                return count
            # Source frame1500 independently annotates all seven LEFT arms raised.
            report['raised_left_arms']={'cpu':raised(report['reference']),mode:raised(report['actual'])}
            report['passed']=report['passed'] and report['raised_left_arms']=={'cpu':7,mode:7}
        return report
    except (ValueError,OSError,KeyError,TypeError) as error:
        return dict(passed=False,error=str(error))


def save(path,value):
    temporary=path.with_suffix('.pending'); temporary.write_text(json.dumps(value,indent=2,allow_nan=False)+'\n',encoding='utf-8'); temporary.replace(path)


def run_fixture(args,name):
    root=args.root.resolve(); source=root/name
    if Path(name).name!=name: raise ValueError('fixture name must be a single path component')
    if name not in NAMES: raise ValueError('unknown960 fixture')
    fixture=validate_fixture(source); g=fixture['geometry']
    if sha(source/'input.fp32')!=fixture['input_sha256']: raise ValueError('source fixture input hash mismatch')
    model=root/'upstream/app/src/main/assets'
    for filename,digest in MODEL_HASHES.items():
        if sha(model/filename)!=digest: raise ValueError('model hash mismatch')
    run_id=args.gpu_mode+'-'+uuid.uuid4().hex
    directory=root/'device-runs'/name/run_id; directory.mkdir(parents=True)
    for filename in ('fixture.json','input.fp32','source.png'): shutil.copyfile(source/filename,directory/filename)
    shutil.copyfile(args.runner,directory/'runner')
    remote='/data/local/tmp/hv-yolo-'+uuid.uuid4().hex
    prefix=[args.adb,'-s',args.serial]
    runner_source,runner_cmake=runner_recipe(args.gpu_mode)
    record=dict(schema_version=1,run_id=run_id,state='RUNNING',gpu_mode=args.gpu_mode,serial=args.serial,runner_sha256=sha(directory/'runner'),runner_source_sha256=sha(runner_source),runner_cmake_sha256=sha(runner_cmake),model_hashes=MODEL_HASHES,fixture_sha256=sha(directory/'fixture.json'),input_sha256=sha(directory/'input.fp32'),geometry=g,started_ns=time.time_ns(),stages={})
    manifest=directory/'execution.json'; save(manifest,record)
    def adb(*command):
        completed=subprocess.run(prefix+list(command),stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if completed.returncode: raise RuntimeError('adb failed: '+completed.stdout.decode('utf-8',errors='replace'))
        return completed.stdout
    try:
        record['device_fingerprint']=adb('shell','getprop','ro.build.fingerprint').decode().strip()
        record['device_model']=adb('shell','getprop','ro.product.model').decode().strip()
        if not record['device_fingerprint']: raise ValueError('missing actual device fingerprint')
        adb('shell','mkdir','-p',remote)
        adb('push',str(directory/'runner'),remote+'/runner')
        adb('shell','chmod','755',remote+'/runner')
        for filename in MODEL_HASHES: adb('push',str(model/filename),remote+'/'+filename)
        adb('push',str(directory/'input.fp32'),remote+'/input.fp32')
        for mode in ('cpu',args.gpu_mode):
            record['state']='RUNNING'
            stage=dict(state='RUNNING',mode=mode,run_id=run_id,serial=args.serial,device_fingerprint=record['device_fingerprint'],runner_sha256=record['runner_sha256'],options=execution_options(mode),exit_code=None,output_hashes={})
            record['stages'][mode]=stage; save(manifest,record)
            # Fresh UUID remote/local directories prevent failed extraction
            # from inheriting outputs of any earlier invocation.
            command=prefix+['shell',remote+'/runner',remote+'/yolov8n_pose.ncnn.param',remote+'/yolov8n_pose.ncnn.bin',remote,mode,str(g['width']),str(g['height'])]
            completed=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            log=directory/f'android-{mode}.log'; log.write_bytes(completed.stdout)
            stage.update(exit_code=completed.returncode,log_file=log.name,log_sha256=sha(log))
            if completed.returncode:
                stage['state']='FAILED'; save(manifest,record); raise RuntimeError(f'{mode} runner exit{completed.returncode}')
            for blob in ('out0','out1'):
                filename=f'ncnn-{mode}-{blob}.fp32'; adb('pull',remote+'/'+filename,str(directory/filename)); stage['output_hashes'][filename]=sha(directory/filename)
            stage['state']='SUCCESS'; save(manifest,record)
        record.update(state='SUCCESS',completed_ns=time.time_ns()); save(manifest,record)
        report=compare_directory(directory,args.gpu_mode)
        save(directory/'comparison.json',report)
        print(json.dumps(dict(directory=str(directory),passed=report['passed'],raw=report.get('raw'),error=report.get('error'))))
        return report['passed']
    except Exception as error:
        record.update(state='FAILED',error=str(error),completed_ns=time.time_ns()); save(manifest,record)
        save(directory/'comparison.json',dict(passed=False,error=str(error)))
        print(json.dumps(dict(directory=str(directory),passed=False,error=str(error))))
        return False


def main():
    p=argparse.ArgumentParser(); p.add_argument('--adb',default='adb'); p.add_argument('--serial',required=True)
    p.add_argument('--runner',type=Path,required=True); p.add_argument('--root',type=Path,default=Path('out/android-yolo/rectangle960-eligibility-20261005'))
    p.add_argument('--gpu-mode',choices=('gpu-fp32',),default='gpu-fp32'); p.add_argument('--fixtures',nargs='+',required=True)
    a=p.parse_args()
    if not a.runner.is_file() or a.runner.read_bytes()[:4]!=b'\x7fELF': raise ValueError('Android ELF runner required')
    results=[run_fixture(a,name) for name in a.fixtures]
    raise SystemExit(0 if all(results) else 1)


if __name__=='__main__': main()

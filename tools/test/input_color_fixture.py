"""Locked, offline source-fixture encoding; never a runtime decoded-image path."""
import argparse,hashlib,json,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('--output',required=True);p.add_argument('--matrix',choices=['601','709'],required=True);p.add_argument('--range',choices=['Full','Limited'],required=True);p.add_argument('--crop',action='store_true');p.add_argument('--startup-timeout',action='store_true');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[2];out=pathlib.Path(a.output);out.mkdir(parents=True,exist_ok=True)
source=root/'out/input/task4-round1/asymmetric-video-1-h264.mp4';ffmpeg=pathlib.Path('C:/ffmpeg/bin/ffmpeg.exe')
sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
lock=json.loads(subprocess.check_output(['py','-3.13',str(root/'tools/test/rtsp_fixture_manifest.py')],text=True))
if sha(source)!='926df205b1a8d3e1cf3e0ebebbdc4e9232f5a0f8d2eb65c2b918c372f2668cfc' or sha(ffmpeg)!=lock['ffmpeg_sha256']:raise SystemExit('Unqualified color source/compiler')
fixture=out/'controlled-color-h264.mp4';matrix='bt709' if a.matrix=='709' else 'bt601';range_name='full' if a.range=='Full' else 'limited';tag='bt709' if a.matrix=='709' else 'smpte170m'
vf=f'drawbox=x=96:y=0:w=48:h=48:color=0x808080:t=fill,drawbox=x=144:y=0:w=48:h=48:color=black:t=fill,scale=in_color_matrix=bt601:out_color_matrix={matrix}:in_range=limited:out_range={range_name}'
command=[str(ffmpeg),'-hide_banner','-nostdin','-y','-i',str(source),'-an','-vf',vf,'-c:v','libx264','-preset','ultrafast','-pix_fmt','yuv420p','-g','25','-bf','0','-colorspace',tag,'-color_primaries',tag,'-color_trc','bt709','-color_range','pc' if a.range=='Full' else 'tv']
if a.startup_timeout:command[command.index('-i'):command.index('-i')]=['-stream_loop','3'];command+=['-x264-params','keyint=1500:min-keyint=1500:scenecut=0']
if a.crop:command+=['-x264-params','crop-rect=8,8,8,8']
command+=[str(fixture)]
with (out/'color-fixture-encode.log').open('w') as log:subprocess.run(command,check=True,stdout=log,stderr=log)
probe=ffmpeg.parent/'ffprobe.exe';raw=subprocess.check_output([str(probe),'-v','error','-select_streams','v:0','-show_streams','-of','json',str(fixture)],text=True);(out/'color-fixture-stream.json').write_text(raw)
stream=json.loads(raw)['streams'][0];width,height=(624,344) if a.crop else (640,360)
if stream['width']!=width or stream['height']!=height or stream.get('color_space')!=tag or stream.get('color_range')!=('pc' if a.range=='Full' else 'tv'):raise SystemExit('Encoded color/crop stream contract mismatch')
receipt={'matrix':a.matrix,'range':a.range,'crop':a.crop,'expectedWidth':width,'expectedHeight':height,'inset':16 if a.crop else 24,'source_sha256':sha(source),'fixture_sha256':sha(fixture),'ffmpeg_sha256':sha(ffmpeg),'ffprobe_sha256':sha(probe),'command':command,'stream':stream,'reference_rgb':[[1,0,0],[0,.5,0],[0,0,1],[1,1,1],[128/255]*3,[0,0,0]]}
if a.crop:receipt['reference_rgb'] += [[128/255]*3,[0,0,0]]
if a.startup_timeout:
 packets=json.loads(subprocess.check_output([str(probe),'-v','error','-select_streams','v:0','-show_packets','-show_entries','packet=pts_time,flags','-of','json',str(fixture)],text=True))
 keytimes=[float(p['pts_time']) for p in packets['packets'] if 'K' in p['flags']]
 if keytimes!=[0.0] or float(stream['duration'])<30:raise SystemExit('Negative fixture needs one initial keyframe and no boundary within timeout')
 receipt['startup_timeout_negative']={'duration':stream['duration'],'actual_keyframe_pts':keytimes,'configured_native_timeout_ms':5000};(out/'negative-packets.json').write_text(json.dumps(packets,indent=2)+'\n')
(out/'color-fixture.json').write_text(json.dumps(receipt,indent=2)+'\n');print(fixture)

"""Task6 diagnostic checks actual decoded GPU color/counters, not Task7 lifecycle."""
import hashlib,json,pathlib,re,sys
run=pathlib.Path(sys.argv[1]);raw=(run/'device-logcat.txt').read_text(encoding='utf-8',errors='replace');fixture=json.loads((run/'color-fixture.json').read_text())
fail=[]
def require(condition,message):
 if not condition:fail.append(message)
cases=re.findall(r'color_case_pixels=(PASS|FAIL) rotation=(\d+) mirror=(\d+) actual_width=(\d+) actual_height=(\d+)',raw)
require(len(cases)==8,'Expected exactly eight color transform cases')
require({(int(r),int(m)) for _,r,m,_,_ in cases}=={(r,m) for r in (0,90,180,270) for m in (0,1)},'Transform cases missing or duplicated')
for status,rotation,mirror,width,height in cases:
 expected=(fixture['expectedHeight'],fixture['expectedWidth']) if int(rotation) in (90,270) else (fixture['expectedWidth'],fixture['expectedHeight'])
 require(status=='PASS' and (int(width),int(height))==expected,'Actual transformed geometry/pixels failed')
pixels=re.findall(r'actual_gpu_pixel sample=(\d+) matrix=(\d+) range=(Full|Limited) rotation=(\d+) mirror=(\d+) x=(\d+) y=(\d+) rgb=([\d.,]+) max_error=([\d.]+) sequence=(\d+)',raw)
expected_pixels=8*len(fixture['reference_rgb'])
require(len(pixels)==expected_pixels,'Expected exactly '+str(expected_pixels)+' actual GPU color/grayscale/crop-edge points')
require(len({(int(p[0]),int(p[3]),int(p[4])) for p in pixels})==expected_pixels,'Pixel cases duplicated')
errors=[]
for sample,matrix,color_range,rotation,mirror,x,y,rgb,error,sequence in pixels:
 require(matrix==fixture['matrix'] and color_range==fixture['range'],'Color case does not match encoded fixture')
 values=list(map(float,rgb.split(',')));reference=fixture['reference_rgb'][int(sample)];computed=max(abs(a-b) for a,b in zip(values,reference));errors.append(computed)
 require(len(values)==3 and computed<.07 and abs(computed-float(error))<.00003,'RGB reference comparison failed')
lines=re.findall(r'gpu_color_counters ([^\r\n]+)',raw);require(len(lines)==1,'Missing/duplicated final counters')
counters={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',lines[-1] if lines else '')}
for name in ('submits','completes','release_exports','ownership_acquires','ownership_returns','queue_callbacks'):require(counters.get(name,0)==counters.get('submits',-1) and counters.get(name,0)>=100,'GPU protocol counter mismatch '+name)
require(counters.get('imports',0)>0 and counters.get('imports')==counters.get('destroys') and counters.get('imports',9999)<counters.get('submits',0),'Imported cache reuse/retirement failed')
expected_fault='--expected-target-view-failure' in sys.argv[2:]
expected_views=9 if expected_fault else 8
require(counters.get('target_views_created')==expected_views and counters.get('target_views_destroyed')==expected_views,'Unity target views did not retire')
for resource in ('source_views','pipelines','descriptor_pools'):
 require(counters.get(resource+'_created',0)>0 and counters.get(resource+'_created')==counters.get(resource+'_destroyed'),'Cached GPU resources did not retire: '+resource)
expected_matrix=1 if fixture['matrix']=='601' else 2;expected_range=1 if fixture['range']=='Full' else 2
completed=re.findall(r'gpu_color_completed [^\r\n]+',raw)
require(len(completed)==counters.get('completes'),'GPU completion metadata count mismatch')
require(all(f'source_matrix={expected_matrix} source_range={expected_range}' in row and 'color_space=0' in row for row in completed),'Actual matrix/range or Unknown transfer output metadata differs')
expected_errors=1 if expected_fault else 0
for name in ('errors','cache_live','cpu_image_readbacks'):require(counters.get(name)==(expected_errors if name=='errors' else 0),'Unexpected GPU failure/live/readback '+name)
if expected_errors:require(raw.count('target_view_fault_injected=true retired_handle_null=1')==1 and 'target_view_replacement_old_retired=true' in raw,'Target view fault not injected exactly once after old view retirement')
require(counters.get('already_complete_acquires',0)+counters.get('positive_acquire_waits',0)==counters.get('submits',-1),'Acquire fence accounting mismatch')
require('foreign_extension_successful_device=1' in raw and 'enabled_extension=VK_EXT_queue_family_foreign' in raw,'No successful logical foreign extension proof')
require(raw.count('unity_target_initialized_before_bind=true')==8,'Missing explicit Unity target initialization ordering')
require('decoder_resources_closed=true active_images=0 active_ahb_references=0 active_owned_fds=0' in raw,'Decoded source resources not returned')
require('decoder_first_keyframe=true' in raw,'No actual startup keyframe admitted')
require(f'source_h264_vui valid_sps=1 present=1 matrix={expected_matrix} range={expected_range}' in raw,'No reliable actual stream VUI color declaration')
require('visible_diagnostic_completed=true native_target_unbound_before_destroy=true' in raw,'Visible diagnostic/preview retirement proof missing')
expected_fault_error='color_result=FAIL stage=create actual Unity storage view code=-1'
if expected_fault:require(raw.count(expected_fault_error)==1,'Missing/duplicated expected target create error')
failure_raw=raw.replace(expected_fault_error,'expected_target_create_error') if expected_fault else raw
require(not re.search(r'color_result=FAIL|color_pixels=FAIL|capability_result=FAIL|Fatal signal',failure_raw),'Unexpected device failure/crash logged')
native=(run/'apk-libhumanvision_input.so').read_bytes();candidate=(run/'libhumanvision_input.so').read_bytes();build=json.loads((run/'build-source-identity.json').read_text())
require(native==candidate and hashlib.sha256(native).hexdigest()==build['native_sha256'],'Actual APK native differs from qualified build')
receipt={'status':'FAIL' if fail else 'PASS','failures':fail,'case_count':len(cases),'pixel_count':len(pixels),'max_rgb_error':max(errors,default=None),'counters':counters,'fixture_sha256':fixture['fixture_sha256'],'qualified_native_sha256':build['native_sha256'],'actual_positive_acquire_fd_wait_exercised':counters.get('positive_acquire_waits',0)>0,'source_transfer_domain':'AndroidMediaFormat codes; no sRGB assumption','source_generation':'actual decoded/completed native records','diagnostic_readbacks':8,'production_cpu_image_readbacks':counters.get('cpu_image_readbacks')}
(run/'color-analysis.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2));sys.exit(1 if fail else 0)

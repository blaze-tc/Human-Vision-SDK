import pathlib,re,json,sys
run=pathlib.Path(sys.argv[1]);raw=(run/'device-logcat.txt').read_text(encoding='utf-8',errors='replace')
fields=lambda line:{k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',line)}
counters=re.findall(r'gpu_color_counters[^\r\n]+',raw);ring=re.findall(r'input_ring_counters[^\r\n]+',raw)
errors=[]
if not counters:errors.append('missing actual GPU counters')
else:
 c=fields(counters[-1])
 for a,b in [('imports','destroys'),('submits','completes'),('target_views_created','target_views_destroyed'),('source_views_created','source_views_destroyed'),('pipelines_created','pipelines_destroyed'),('descriptor_pools_created','descriptor_pools_destroyed'),('submits','release_exports'),('submits','ownership_acquires'),('submits','ownership_returns')]:
  if c.get(a)!=c.get(b):errors.append(a+' does not balance '+b)
 if c.get('submits',0)<100 or any(c.get(k,-1)!=0 for k in ['errors','cache_live','cpu_image_readbacks']):errors.append('GPU errors/resources/readbacks or insufficient real submissions')
if not ring or fields(ring[-1]).get('slots_live')!=0:errors.append('output slots not retired')
play=re.findall(r'lifecycle_playback_seconds=([\d.]+)',raw)
if not play or float(play[-1])<60:errors.append('60s actual playback missing')
trans=re.findall(r'lifecycle_transition=(\d+)[^\r\n]+',raw)
if sorted(map(int,trans))!=list(range(10)):errors.append('ten lifecycle transitions missing')
final=re.findall(r'lifecycle_result=PASS[^\r\n]+',raw)
if not final or fields(final[-1]).get('overlapping_native_copies',0)<1:errors.append('Close while actual queued GPU copy missing')
held=set();hold_count=transfer_count=release_peak=owned_peak=0
resource_records=[]
for line in raw.splitlines():
 f=fields(line)
 if any(k in f for k in ['active_images','active_ahb_references','active_owned_fds']):
  resource_records.append(f)
  if any(f.get(k,0)<0 for k in ['active_images','active_ahb_references','active_owned_fds']):errors.append('negative owned resource count')
  owned_peak=max(owned_peak,f.get('active_owned_fds',0))
 if 'release_fd_held' in f:
  fd=f['release_fd_held'];hold_count+=1
  if fd<0 or fd in held:errors.append('invalid or duplicate release-fd hold')
  held.add(fd);release_peak=max(release_peak,len(held))
 if 'release_fd_transferred_to_AImage' in f:
  fd=f['release_fd_transferred_to_AImage'];transfer_count+=1
  if fd not in held:errors.append('release-fd transfer without matching hold')
  else:held.remove(fd)
 if ('release_fd_held' in f or 'release_fd_transferred_to_AImage' in f) and f.get('active_owned_fds',-1)<len(held):errors.append('owned fd count below outstanding release holds')
if not hold_count or hold_count!=transfer_count or held:errors.append('release-fd holds/transfers not balanced at terminal baseline')
if counters and fields(counters[-1]).get('release_exports')!=hold_count:errors.append('release-fd hold count does not match terminal GPU export count')
if owned_peak<release_peak:errors.append('owned fd peak below actual release-fd peak')
if 'consumer_copy_resources_retired=1 command_pool_live=0 fence_live=0' not in raw:errors.append('diagnostic actual copy resources not retired')
if raw.count('consumer_source_copy_submitted=')!=10 or raw.count('consumer_source_copy_completed=')!=10:errors.append('ten real consumer source copies did not submit/complete')
if 'real_output_saturation_pass=1 held_slots=3 published_frame_unchanged=1' not in raw:errors.append('actual saturated output slots not protected')
if 'production_pause_closed=true' not in raw or 'production_resume_requested=true' not in raw:errors.append('actual production app pause/resume absent')
if 'encoded_backlog_controlled_reconnect=1 wait_for_keyframe=1 stage=read compressed H264' not in raw:errors.append('controlled physical disconnect/reconnect absent')
if 'lifecycle_result=FAIL'  in raw or 'Fatal signal' in raw:errors.append('actual player failure')
closures=re.findall(r'decoder_resources_closed=true[^\r\n]+',raw)
if not closures or any(fields(closures[-1]).get(k,-1)!=0 for k in ['active_images','active_ahb_references','active_owned_fds']):errors.append('terminal decoder/image/AHB/fd baseline missing')
for k in ['active_images','active_ahb_references','active_owned_fds']:
 values=[f[k] for f in resource_records if k in f]
 if not values or values[-1]!=0:errors.append('terminal '+k+' is not zero')
result={'status':'FAIL' if errors else 'PASS','errors':errors,'release_fd_holds':hold_count,'release_fd_transfers':transfer_count,'release_fd_peak':release_peak,'owned_fd_peak':owned_peak,'terminal_release_fds':len(held),'positive_acquire_fd_hardware':'unqualified unless actual positive wait counter > 0','serialized_converter':True}
(run/'lifecycle-analysis.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result));sys.exit(bool(errors))

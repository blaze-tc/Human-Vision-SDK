"""Verify Task8's actual copy/retirement gate, independently of inference."""
import json
import pathlib
import re
import sys

run = pathlib.Path(sys.argv[1])
raw = (run / 'logcat.txt').read_text(encoding='utf-8', errors='replace')
fields = lambda line: {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', line)}
errors = []
colors = re.findall(r'gpu_color_counters[^\r\n]+', raw)
if not colors:
    errors.append('missing actual input GPU resource counters')
else:
    c = fields(colors[-1])
    for a, b in [('imports', 'destroys'), ('submits', 'completes'),
                 ('target_views_created', 'target_views_destroyed'),
                 ('source_views_created', 'source_views_destroyed'),
                 ('pipelines_created', 'pipelines_destroyed'),
                 ('descriptor_pools_created', 'descriptor_pools_destroyed'),
                 ('submits', 'release_exports'), ('submits', 'ownership_acquires'),
                 ('submits', 'ownership_returns')]:
        if a not in c or b not in c or c[a] != c[b]:
            errors.append(a + ' does not balance ' + b)
    if c.get('submits', 0) <= 0:
        errors.append('no real input GPU submissions')
    for k in ['errors', 'cache_live', 'cpu_image_readbacks']:
        if c.get(k, -1) != 0:
            errors.append('terminal input ' + k + ' is not zero')
ring = re.findall(r'input_ring_counters[^\r\n]+', raw)
if not ring or fields(ring[-1]).get('slots_live', -1) != 0:
    errors.append('terminal output slots not retired')
held = set()
hold_count = transfer_count = 0
resources = []
for line in raw.splitlines():
    f = fields(line)
    if any(k in f for k in ['active_images', 'active_ahb_references', 'active_owned_fds']):
        resources.append(f)
        if any(f.get(k, 0) < 0 for k in ['active_images', 'active_ahb_references', 'active_owned_fds']):
            errors.append('negative signed resource count')
    if 'release_fd_held' in f:
        fd = f['release_fd_held']
        hold_count += 1
        if fd < 0 or fd in held:
            errors.append('invalid or duplicate release FD hold')
        held.add(fd)
    if 'release_fd_transferred_to_AImage' in f:
        fd = f['release_fd_transferred_to_AImage']
        transfer_count += 1
        if fd not in held:
            errors.append('release FD transfer without hold')
        else:
            held.remove(fd)
if hold_count <= 0 or hold_count != transfer_count or held:
    errors.append('release FD holds/transfers do not balance')
if colors and fields(colors[-1]).get('release_exports', -1) != hold_count:
    errors.append('GPU exports do not match actual FD holds')
closures = re.findall(r'decoder_resources_closed=true[^\r\n]+', raw)
for k in ['active_images', 'active_ahb_references', 'active_owned_fds']:
    values = [r[k] for r in resources if k in r]
    if not closures or fields(closures[-1]).get(k, -1) != 0 or not values or values[-1] != 0:
        errors.append('terminal decoder ' + k + ' is not zero')
required = [r'HVTask8 close_complete.+native_copy_complete=1 consumer_held=1 source_stopped=1',
            r'HVTask8 old_generation_blocked=1',
            r'HVTask8 new_sdk_admission=1 configuration_pending=1 consumer_held=1',
            r'HVTask8 new_sdk_copy_completed=1',
            r'HVTask8 result=PASS terminal_source_stopped=1 consumer_held=0',
            r'input_only_preinit_hook=1',
            r'device_creation success.+ycbcr_enabled=1 sync_fd_extension_enabled=1 ahb_extension_enabled=1']
positions = [re.search(pattern, raw) for pattern in required[:5]]
if all(positions) and [m.start() for m in positions] != sorted(m.start() for m in positions):
    errors.append("actual copy/retirement/new-generation sequence is out of order")
for pattern in required:
    if not re.search(pattern, raw):
        errors.append('missing actual sequence evidence: ' + pattern)
sdk = re.findall(r'HVTask8 sdk_terminal[^\r\n]+', raw)
if not sdk:
    errors.append('missing SDK terminal resource snapshot')
else:
    c = fields(sdk[-1])
    for k in ['slots_live', 'source_views_live', 'consumer_held', 'consumer_fd_live', 'consumer_ahb_live', 'copy_errors']:
        if c.get(k, -1) != 0:
            errors.append('terminal SDK ' + k + ' is not zero')
    if c.get('bridge_closed') != 1 or c.get('successful_copies', 0) <= 0:
        errors.append('SDK final generation did not copy and drain')
if re.search(r'HVTask8 result=FAIL|Fatal signal|color_result=FAIL', raw):
    errors.append('physical gate failure')
result = dict(status='FAIL' if errors else 'PASS', errors=errors,
              release_fd_holds=hold_count, release_fd_transfers=transfer_count,
              terminal_release_fds=len(held),
              boundary='private SDK GPU copy + actual ConsumerFrame occupancy; no inference',
              positive_acquire_fd_hardware='unqualified unless actual positive wait counter > 0')
(run / 'resource-analysis.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
sys.exit(bool(errors))

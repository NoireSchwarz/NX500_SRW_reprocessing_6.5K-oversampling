from pathlib import Path
import hashlib,json
W=Path(__file__).resolve().parent;R=W/'r10j-frame-flow';B=W/'r10i-hardware-qos';V=R/'validation/current'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
evidence={}
for name in ['timing-build.json','timing-ARM.json','video-rtos-build.json','video-fullwidth-replay.json','video-host.json','video-observer.json','video-probe-os.json','path-reader.json','flow-production.json']:
 d=json.loads((V/name).read_text('utf8'));assert d['passed'],name;evidence[name]=sha(V/name)
 if 'binary_sha256'in d:
  binary=R/'base/nx-lut-lite/bin'/('nxl-video-fullwidth.arm'if name=='video-host.json'else'nxl-video-probe.arm')
  assert d['binary_sha256']==sha(binary),name
 if 'source_sha256'in d:assert d['source_sha256']==sha(R/'core/src'/('rtos_video_fullwidth.c'if name=='video-rtos-build.json'else'nxl-video-probe.c')),name
frozen={}
for p in(B/'base/nx-lut-lite/bin').iterdir():
 if p.is_file() and p.name not in ['nxl-menu','nxl-video-probe.arm','nxl-video-fullwidth.arm']:
  assert sha(p)==sha(R/'base/nx-lut-lite/bin'/p.name),p;frozen[p.name]=sha(p)
blue=json.loads((V/'timing-build.json').read_text('utf8'))['blocks'][0]
assert blue['full_sha256']=='5d43df291078dcb87437bda82035ac7f3a8b1125bdb1e3280f4f09ff493c6131'
changes={};skip={'bin','build','results','validation','__pycache__','.git','.zig-cache','zig-cache'}
for p in R.rglob('*'):
 n=p.relative_to(R)
 if p.is_file() and not any(x in skip or x.startswith('build-')for x in n.parts) and (not(B/n).is_file()or p.read_bytes()!=(B/n).read_bytes()):changes[n.as_posix()]=sha(p)
report=dict(passed=True,baseline='Camera-tested R10i',changed_sources=changes,frozen_SHA256=frozen,
 blue_fix_preserved=True,blue_recipe_sha256=blue['full_sha256'],sample_schema=6,header_bytes=16384,record_bytes=1152,sampler_RAM_bytes=6928384,
 RTOS_ring_bytes=256,RTOS_count_lease_bytes=280,RTOS_ring_capacity=8,RTOS_producer_schema=4,
 no_extra_RTOS_heap_SMA_or_frame_buffer=True,no_hot_SD_writes=True,HEVC_bridge_writes=False,CPU_or_DDR_clock_writes=False,
 native_ACK_and_mask_changes=False,sampling_target_access_readonly=True,trace_only_arm_changes_selected_output=False,
 IRQ_timestamp_semantics='First IRQ44 observations before unchanged native ACK; not hardware-latched assertion times',
 EP_readback_reinvestigation=False,frame_rate_fixed=False,camera_hardware_tested=False,
 evidence_SHA256=evidence,rebuilt_SHA256={n:sha(R/'base/nx-lut-lite/bin'/n)for n in ['nxl-menu','nxl-video-probe.arm','nxl-video-fullwidth.arm']})
(V/'build.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
print('PASS R10j scope, blue recipe, frozen SRW/USB engines and actual ARM evidence')

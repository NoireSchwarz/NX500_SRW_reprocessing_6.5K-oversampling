from pathlib import Path
import hashlib,json,shutil,stat,subprocess,sys,zipfile
W=Path(__file__).resolve().parent;R=W/'r10j-frame-flow';B=W/'r10i-hardware-qos-full-v1';P=W/'r10j-frame-flow-full-v1';O=W.parent/'outputs';V=R/'validation/current';M='nx-lut-demo/nx-lut-lite/';EV='EVIDENCE/R10J_FRAME_FLOW/'
sys.path[:0]=[str(W),str(R/'tools')]
from audit_camera_manifest import audit_package
from runtime_identity import bind,audit,sha,PATTERN,target
from validate_package_bootstrap import validate
def cp(src,n):p=P/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,p)
def put(n,s):p=P/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,encoding='utf8',newline='\n')
def evidence(final=False):
 names=['build.json','timing-build.json','timing-ARM.json','video-rtos-build.json','video-fullwidth-replay.json','video-host.json','video-observer.json','video-probe-os.json','path-reader.json','flow-production.json']+(['runtime.json','prior-installed-upgrade.json']if final else[])
 for n in names:assert json.loads((V/n).read_text('utf8'))['passed'];cp(V/n,EV+n)
 for n in ['r10j_timing.S','build_r10j_timing.py','build_r10j_rtos.py','build_r10j.py','test_r10j_timing.py','test_r10j_fullwidth.py','test_r10j_host.py','test_r10j_flow_production.py','test_r10j_probe_os.py','test_r10j_path_probe.py','test_r10j_runtime.py','validate_r10j_install.py','prepare_r10j_release.py','package_r10j.py','deploy_sd_r10j.py']:
  cp(W/n,'SOURCE/packaging/'+n)
 for n in ['R10i_BUS_QOS_ABA与周期阻塞诊断_20261110.txt','R10i_BUS_QOS_ABA与周期阻塞诊断_20261110.json','R10i_NX1访存与握手核对_20261110.txt']:
  cp(O/n,EV+'baseline/'+n)
 cp(O/'R10j_测试步骤与更新说明.txt','R10j_更新说明.txt')
 for helper in ['video_frame_flow.py','video_hw_timing.py','video_qos.py','video_bus.py','video_stage_timing.py','video_completion.py','video_hardware_progress.py','analyze_video_counters.py']:cp(R/'tools'/helper,'PC/video-stage-counters/'+helper)
 cp(R/'base/nx-lut-lite/data/video-probe-schema.json','PC/video-stage-counters/video-probe-schema.json')
if '--finalize'not in sys.argv:
 old=json.loads((O/'R10i_HARDWARE_QOS_PACKAGE_VALIDATION.json').read_text('utf8'));assert sha(O/old['archive'])==old['sha256']and audit_package(B)['passed']
 fresh=not P.exists();assert fresh or '--refresh'in sys.argv
 if fresh:shutil.copytree(B,P)
 names=[line.decode().split(' ',1)[1]for line in(B/'nx-lut-demo/module.manifest').read_bytes().splitlines()]
 for n in names:
  p=R/'base/nx-lut-lite'/n
  if p.is_file()and(not n.startswith('bin/')or n in ['bin/nxl-menu','bin/nxl-video-probe.arm','bin/nxl-video-fullwidth.arm']):cp(p,M+n)
 cp(R/'installer/deploy.sh','nx-lut-demo/deploy.sh');cp(R/'installer/install-entry.sh','install.sh')
 if fresh:
  cp(B/'nx-lut-demo/module.manifest','nx-lut-demo/ownership/r10i.manifest')
  put('nx-lut-demo/ownership/index.sha256',sha(P/'nx-lut-demo/ownership/r10i.manifest')+' r10i.manifest\n'+(P/'nx-lut-demo/ownership/index.sha256').read_text('utf8'))
 notes=(O/'R10j_测试步骤与更新说明.txt').read_text('utf8')
 for n in ['README_zh.md','DEMO_SCOPE_zh.md',M+'README_zh.md']:put(n,notes)
 skip={'bin','build','results','validation','__pycache__','.git','.zig-cache','zig-cache'}
 for p in R.rglob('*'):
  n=p.relative_to(R)
  if p.is_file()and not any(x in skip or x.startswith('build-')for x in n.parts):cp(p,'SOURCE/demo-p52-recovery-r7/'+n.as_posix())
 evidence();bind(P);names=sorted(p.relative_to(P/M).as_posix()for p in(P/M).rglob('*')if p.is_file());assert len(names)==64
 put('nx-lut-demo/module.manifest',''.join(sha(P/M/n)+' '+n+'\n'for n in names));n='nx-lut-demo/deploy.sh';put(n,PATTERN.sub(lambda m:m[1]+'='+sha(P/target(n,m[1])),(P/n).read_text('utf8')))
 assert audit(P)['passed']and audit_package(P,include_sums=False)['passed']
 for p in(B/M/'bin').iterdir():
  if p.is_file()and p.name not in ['nxl-menu','nxl-video-probe.arm','nxl-video-fullwidth.arm']:assert sha(p)==sha(P/M/'bin'/p.name)
 print('Staged R10j complete package:64 module files; same blue recipe and SRW/USB engines; new three-mode flow trace');raise SystemExit
evidence(final=True);(R/'validation/r7h').mkdir(parents=True,exist_ok=True);bootstrap=validate(P);assert bootstrap['passed'];put(EV+'bootstrap.json',json.dumps(bootstrap,indent=2)+'\n')
shells=[]
for p in P.rglob('*.sh'):
 r=subprocess.run(['C:/Program Files/Git/usr/bin/sh.exe','-n',p.resolve().as_posix()],capture_output=True);assert not r.returncode,(p,r.stderr);shells.append(p.relative_to(P).as_posix())
identity=audit(P);assert identity['passed']and audit_package(P,include_sums=False)['passed']
info=json.loads((B/'BUILD.json').read_text('utf8'))
for k in list(info):
 if k.startswith(('CPU_AB','HEVC_bridge_AB'))or k in ['HEVC_bridge_candidate','HEVC_bridge_only_write','frame_drop_fix_candidate']:info.pop(k,None)
info.update(format='NXL_R10J_FRAME_FLOW_TEST1',install_revision='recovery-r10j-frame-flow',test_package=True,
 final_output=[3840,2160],LDC_UHD_boundary=[3846,2168],blue_recipe_sha256='5d43df291078dcb87437bda82035ac7f3a8b1125bdb1e3280f4f09ff493c6131',
 blue_edge_fixed=True,blue_boundary_implemented=True,frame_drop_fixed=False,camera_hardware_tested=False,SD_deployed=False,
 sample_schema=6,sample_record_bytes=1152,header_bytes=16384,sampler_RAM_bytes=6928384,
 IPC_hardware_IRQ_schema=4,IPC_EP_ring_bytes=256,IPC_EP_ring_capacity=8,TMC_count_facility_bytes=280,
 RTOS_machine_code_identical_to_camera_tested_R10f=False,RTOS_machine_code_identical_to_R10h=False,
 stage_events=['hardware commit after original STR and DSB','first SRAM-load IRQ43 observation','first CS0-start IRQ46 observation','first CS0-done IRQ42 observation','qualified callback5'],
 trace_driver_mode_pairs=['1717','1818','1919'],trace_presets=['E004','E005'],trace_only_arm_preserves_selected_output=True,
 hardware_progress_words=32,pinned_hardware_phase_capacity=16,pinned_hardware_phase_offset=4096,pinned_hardware_phase_bytes=11296,
 phase_settle_ms=2000,flow_context_record_word_offset=255,flow_context_words=32,
 HEVC_bridge_AB_opt_in=False,HEVC_bridge_writes=False,CPU_governor_writes=False,DDR_writes=False,codec_clock_writes=False,
 new_IRQ_ACK_writes=False,new_IRQ_mask_writes=False,no_MPTOP_device_open=True,no_ioctl=True,hot_SD_writes=False,
 new_native_timer_reads=True,native_timer_read_order=['native high','native low'],
 first_apply_configuration_stores=327,later_apply_restore_stores=112,trace_only_first_arm_stores=285,
 guarded_replacement_words=242,extra_frame_allocation=False,IPC_EP_extra_heap_or_SMA_bytes=0,EP_readback_reinvestigation=False,
 stage_counter_target_access='Read-only5Hz sampling and pure bookmarks. Guarded idle trace arming only; no selected geometry/clock/codec changes. OEM IRQ ACK/masks unchanged.',
 validation_scope='Actual ARM instructions, mocked OS/cache endpoints, no physical throughput proof')
put('BUILD.json',json.dumps(info,indent=2)+'\n');put('installer-identities.json',json.dumps(identity,indent=2)+'\n')
files=sorted(p for p in P.rglob('*')if p.is_file()and p.relative_to(P).as_posix()!='SHA256SUMS');put('SHA256SUMS',''.join(sha(p)+'  '+p.relative_to(P).as_posix()+'\n'for p in files));assert audit_package(P)['passed']
archive=O/'NX_LUT_Lite_0.4.0_demo51_R10j_frame_flow_test_full.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6)as z:
 for p in sorted(p for p in P.rglob('*')if p.is_file()):
  n=p.relative_to(P).as_posix();i=zipfile.ZipInfo(n,(2026,11,10,1,0,0));i.compress_type=zipfile.ZIP_DEFLATED;i.external_attr=(stat.S_IFREG|(0o755 if'/bin/'in n or p.suffix in['.sh','.arm']else 0o644))<<16;z.writestr(i,p.read_bytes())
with zipfile.ZipFile(archive)as z:
 assert z.testzip()is None;names=z.namelist();assert len(names)==len(set(n.casefold()for n in names));checks={n:h for h,n in(line.split('  ',1)for line in z.read('SHA256SUMS').decode().splitlines())};assert set(checks)==set(names)-{'SHA256SUMS'};assert all(hashlib.sha256(z.read(n)).hexdigest()==h for n,h in checks.items())
report=dict(passed=True,archive=archive.name,sha256=sha(archive),bytes=archive.stat().st_size,zip_entries=len(names),module_files=64,identity_pins=len(identity['checks']),shell_files=len(shells),raw_camera_text_audit_passed=True,
 SRW_USB_video80_engines_unchanged=True,blue_recipe_preserved=True,test_package=True,sample_schema=6,sample_record_bytes=1152,header_bytes=16384,sampler_RAM_bytes=6928384,
 boot_resident_fixed_TMC_code=True,RTOS_ring_bytes=256,RTOS_ring_capacity=8,native_event_flow_schema=4,trace_only_arm=True,
 HEVC_bridge_AB_opt_in=False,HEVC_bridge_writes=False,no_hot_SD_writes=True,extra_frame_allocation=False,
 frame_drop_fixed=False,camera_hardware_tested=False,SD_deployed=False)
(O/'R10j_FRAME_FLOW_PACKAGE_VALIDATION.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8',newline='\n');(O/(archive.stem+'.sha256')).write_text(report['sha256']+'  '+archive.name+'\n',encoding='ascii',newline='\n')
state=dict(release='R10j',state='LOCAL_PACKAGE_VERIFIED_DEPLOYMENT_PENDING',archive=str(archive.resolve()),archive_sha256=report['sha256'],SD_accessed=True,SD_deployed=False,camera_install_executed=False,camera_hardware_tested=False,test_package=True)
for p in [W/'r10j-state.json',O/'R10j_FRAME_FLOW_RELEASE_STATE.json']:p.write_text(json.dumps(state,indent=2)+'\n',encoding='utf8',newline='\n')
print(json.dumps(report,indent=2))

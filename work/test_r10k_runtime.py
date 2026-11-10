from pathlib import Path
import ast,hashlib,io,json,struct,sys,zlib
W=Path(__file__).resolve().parent;R=W/'r10k-ep-flow';P=W/'r10k-ep-flow-full-v1';V=R/'validation/current';M='nx-lut-demo/nx-lut-lite/'
sys.path[:0]=[str(W/'tooling/python'),str(R/'tools')]
from post_file_model import PostFileModel
from unicorn.arm_const import *
from elftools.elf.elffile import ELFFile
txt=(W/'test_r8k_runtime.py').read_text('utf8')
for x in ast.parse(txt).body:
 if isinstance(x,ast.ClassDef)and x.name=='RuntimeModel':exec(ast.get_source_segment(txt,x))
cases=[]
for name in ['nxl-demo-front.arm','nxl-init-pipeline.arm','nxl-demo-post.arm','nxl-session-rearm.arm','nxl-oem-restore.arm','nxl-lens-watch.arm','nxl-owned-ep.arm','nxl-video-mode.arm','nxl-video-fullwidth.arm','nxl-usb-guard.arm','nxl-video-probe.arm']:
 r=RuntimeModel(P/M/'bin'/name,['--version'],V).run();assert r['exit']==0,(name,r);assert not any(x in r['modeled_syscalls']for x in [26,37,117,180]),(name,r)
 if name in ['nxl-demo-front.arm','nxl-init-pipeline.arm']:assert 'SHARED192'in r['stdout']and'R9N_FULLWIDTH_POWER'in r['stdout']
 if name=='nxl-usb-guard.arm':assert 'R9u' in r['stdout'] and 'SAME_INODE' in r['stdout']
 if name=='nxl-video-fullwidth.arm':assert 'R10j' in r['stdout'] and 'PP19_RAW12_6496_3644_TO_NATIVE_3840_2160' in r['stdout']
 if name=='nxl-video-probe.arm':assert 'R10k' in r['stdout']
 if name=='nxl-owned-ep.arm':assert 'R9G'in r['stdout'].upper()
 if name=='nxl-lens-watch.arm':assert'R9U'in r['stdout'].upper()
 cases.append(dict(name=name,**r));print('PASS packaged ARM version',name,flush=True)
plan=(R/'plan.json').read_bytes();header=(R/'core/src/p48_plan_hashes.h').read_text('utf8');meta=json.loads(plan)
assert hashlib.sha256(plan).hexdigest()in header and ('0x%08xu'%zlib.crc32(plan))in header and meta['cap_bytes']==201326592 and meta['ldc_injection_enabled']
assert meta['case_hashes']==[hashlib.sha256(json.dumps(meta['case'],sort_keys=True,separators=(',',':'),ensure_ascii=False).encode()).hexdigest()]
cases.append(dict(name='source_plan_hash_CRC_and_capacity',plan_sha256=hashlib.sha256(plan).hexdigest(),cap_bytes=meta['cap_bytes']))
elf=[]
for name in ['nxl-hw-worker.arm','nxl-menu']:
 data=(P/M/'bin'/name).read_bytes();e=ELFFile(io.BytesIO(data));assert e.elfclass==32 and e['e_machine']=='EM_ARM'and e['e_flags']&0x200 and not e['e_flags']&0x400
 deps=[t.needed for s in e.iter_segments()if s['p_type']=='PT_DYNAMIC'for t in s.iter_tags()if t.entry.d_tag=='DT_NEEDED'];assert set(deps)==({'libc.so.6','libudd5.so'}if name=='nxl-hw-worker.arm'else{'libc.so.6','libX11.so.6'}),(name,deps);elf.append(dict(name=name,dependencies=deps,sha256=hashlib.sha256(data).hexdigest()))
menu=(P/M/'bin/nxl-menu').read_bytes();assert b'R10k EP + FRAME FLOW'in menu and all(x in menu for x in [b'PIPE_SOURCE_VERIFY',b'PIPE_WORKSPACE',b'PIPE_RAW_DECODE',b'PIPE_LDC',b'STAGE COUNTERS',b'4K30P / 80M',b'UNPLUG USB TO START TEST RECORDER',b'USB STATE UNKNOWN - TEST RECORDER REFUSED'])
(V/'runtime.json').write_text(json.dumps(dict(passed=True,cases=cases,original_ABI_ELF=elf,hardware_tested=False),indent=2)+'\n',encoding='utf8',newline='\n')

"""Bind and verify every active shell identity against the final package.

The historical OLD_FRONT_SHA is an admission whitelist, not a live component.
Its exact value stays fixed. Dependency scripts are bound before their caller.
"""
from pathlib import Path
import argparse,hashlib,json,re
M='nx-lut-demo/nx-lut-lite/'
LIVE={
 'VIDEO_PROBE_SHA':M+'bin/nxl-video-probe.arm',
 'USB_GUARD_SHA':M+'bin/nxl-usb-guard.arm',
 'VIDEO_FULLWIDTH_SHA':M+'bin/nxl-video-fullwidth.arm',
 'VIDEO_MODE_SHA':M+'bin/nxl-video-mode.arm',
 'INIT_PIPELINE_SHA':M+'bin/nxl-init-pipeline.arm','OWNED_EP_SHA':M+'bin/nxl-owned-ep.arm',
 'LENS_WATCH_SHA':M+'bin/nxl-lens-watch.arm','LENS_CAPTURE_SHA':M+'lens-capture.sh','RUNNER_SHA':M+'memory-test.sh',
 'INIT_TEST_SHA':M+'bin/nxl-init-test.arm','AUTO_WATCH_SHA':M+'bin/nxl-init-watch.arm','AUTO_CAPTURE_SHA':M+'auto-init-capture.sh',
 'WATCH_SHA':M+'bin/nxl-handoff-watch.arm',
 'HANDOFF_WATCH_SHA':M+'bin/nxl-handoff-watch.arm',
 'CV_SHA':M+'bin/cube2lut.arm','OPS_SHA':M+'bin/nxl-fileops','SESSION_SHA':M+'bin/nxl-session.arm',
 'MENU_SHA':M+'bin/nxl-menu','PROBE_SHA':M+'bin/nxl-session-rearm.arm',
 'RESTORE_SHA':M+'bin/nxl-oem-restore.arm',
 'RESTORE_SCRIPT_SHA':M+'restore-camera.sh','REARM_SCRIPT_SHA':M+'rearm-session.sh',
 'RETIRE_MANIFEST_SHA':M+'data/prior-front-retire.sha256',
 'FRONT_SHA':M+'bin/nxl-demo-front.arm','POST_SHA':M+'bin/nxl-demo-post.arm',
 'HWCTL_SHA':M+'bin/nxl-hw-controller.arm','HWWORK_SHA':M+'bin/nxl-hw-worker.arm',
 'ENGINE_SHA':M+'lut.sh','SOURCE_SHA':'nx-lut-demo/assets/SAM_0183.SRW',
 'PLAN_SHA':'SOURCE/demo-p52-recovery-r7/plan.json',
 'MANIFEST_SHA':'nx-lut-demo/module.manifest','HOLDCHECK_SHA':'nx-lut-demo/bin/nxl-holdcheck.arm',
 'SESSION_CHECK_SHA':'nx-lut-demo/bin/nxl-session.arm','OWNERSHIP_INDEX_SHA':'nx-lut-demo/ownership/index.sha256'
}
HISTORICAL={'OLD_FRONT_SHA':'07f64d2ab1a3bc073a49a2b45fffa4ec4ae41fc5ad7dfddfb6384563be9c7a14'}
PATTERN=re.compile(r'^([A-Z][A-Z0-9_]*_SHA)=([0-9a-f]{64})$',re.M)
DECLARATION=re.compile(r'^([A-Z][A-Z0-9_]*_SHA)=(.*)$',re.M)
REQUIRED={
 M+'video-probe.sh':set(['OPS_SHA','VIDEO_PROBE_SHA','VIDEO_FULLWIDTH_SHA']),
 M+'video-mode.sh':set(['OPS_SHA','VIDEO_MODE_SHA','VIDEO_FULLWIDTH_SHA','USB_GUARD_SHA']),
 M+'auto-init.sh':set(['OPS_SHA','INIT_TEST_SHA','LENS_CAPTURE_SHA','RUNNER_SHA']),M+'auto-init-capture.sh':set(['AUTO_WATCH_SHA']),
 M+'lens-capture.sh':set(['OPS_SHA','LENS_WATCH_SHA']),
 M+'memory-test.sh':set(['CV_SHA','HANDOFF_WATCH_SHA','RESTORE_SHA','RESTORE_SCRIPT_SHA','REARM_SCRIPT_SHA','RETIRE_MANIFEST_SHA','SESSION_SHA','FRONT_SHA','POST_SHA','HWCTL_SHA','HWWORK_SHA','MENU_SHA','ENGINE_SHA','OPS_SHA','OLD_FRONT_SHA','SOURCE_SHA','PLAN_SHA']),
 M+'rearm-session.sh':set(['OPS_SHA','SESSION_SHA','PROBE_SHA','MENU_SHA','HOLDCHECK_SHA']),
 M+'restore-camera.sh':set(['OPS_SHA','MENU_SHA','RESTORE_SHA']),M+'handoff-watch.sh':set(['WATCH_SHA']),
 'nx-lut-demo/deploy.sh':set(['MANIFEST_SHA','OPS_SHA','HOLDCHECK_SHA','SESSION_CHECK_SHA','OWNERSHIP_INDEX_SHA'])}
def declarations(name,text):
 rows=DECLARATION.findall(text);keys=[x[0] for x in rows]
 if len(keys)!=len(set(keys)):raise ValueError('Duplicate identity in '+name)
 if not REQUIRED.get(name,set()).issubset(keys):raise ValueError('Missing required identity in '+name)
 for key,value in rows:
  if not re.fullmatch('[0-9a-f]{64}',value):raise ValueError('Malformed identity: '+name+' '+key)
  if key not in HISTORICAL:target(name,key)
 return rows
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def scripts(root):
 root=Path(root);leaf=[p.relative_to(root).as_posix() for p in (root/M).rglob('*.sh')]
 return leaf+['nx-lut-demo/deploy.sh','install.sh']
def target(script,key):
 if script=='nx-lut-demo/deploy.sh' and key=='OPS_SHA':return 'nx-lut-demo/patch-fileops'
 if script==M+'rearm-session.sh' and key=='HOLDCHECK_SHA':return M+'bin/nxl-holdcheck.arm'
 if key not in LIVE:raise ValueError('Unclassified active identity: '+script+' '+key)
 return LIVE[key]
def audit(root):
 root=Path(root);rows=[]
 for name in scripts(root):
  for key,value in declarations(name,(root/name).read_text(encoding='utf8')):
   if key in HISTORICAL:
    rows.append(dict(script=name,key=key,classification='historical whitelist',matches=value==HISTORICAL[key]));continue
   leaf=target(name,key);actual=sha(root/leaf)
   rows.append(dict(script=name,key=key,classification='package dependency',target=leaf,expected=value,actual=actual,matches=value==actual))
 return dict(passed=all(x['matches'] for x in rows),checks=rows,failed=[x for x in rows if not x['matches']])
def bind(root):
 root=Path(root);order=[M+'auto-init-capture.sh',M+'lens-capture.sh',M+'rearm-session.sh',M+'restore-camera.sh',M+'handoff-watch.sh',M+'memory-test.sh',M+'auto-init.sh']
 other=[p for p in scripts(root) if p not in order and p!='nx-lut-demo/deploy.sh'];changed=[]
 for name in other+order:
  p=root/name;s=p.read_text(encoding='utf8')
  declarations(name,s)
  def replace(m):
   key,value=m.groups()
   if key in HISTORICAL:
    if value!=HISTORICAL[key]:raise ValueError('Historical identity changed: '+key)
    return m.group()
   actual=sha(root/target(name,key))
   if actual!=value:changed.append(dict(script=name,key=key,before=value,after=actual))
   return key+'='+actual
  p.write_text(PATTERN.sub(replace,s),encoding='utf8',newline='\n')
 return changed
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('package',type=Path);p.add_argument('--output',type=Path);a=p.parse_args();result=audit(a.package)
 if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
 print(json.dumps(dict(passed=result['passed'],checks=len(result['checks']),failed=result['failed']),indent=2));raise SystemExit(0 if result['passed'] else 1)

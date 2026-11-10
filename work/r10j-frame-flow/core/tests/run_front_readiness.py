#!/usr/bin/env python3
"""PC-only predicate and durable Linux-adapter regression checks."""
from pathlib import Path
import json,subprocess,tempfile,hashlib
root=Path(__file__).resolve().parents[1]
records=[]
with tempfile.TemporaryDirectory(prefix='nxl-readiness-') as temporary:
    temporary=Path(temporary)
    for name in ['test_front_readiness','test_front_readiness_host']:
        executable=temporary/name
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(root/'src'),str(root/'tests'/f'{name}.c'),'-o',str(executable)],check=True)
        if name=='test_front_readiness':
            text=subprocess.check_output([str(executable)],text=True).strip()
            print(text);records.append({'test':name,'result':text})
        else:
            for mode in ['pass','reject']:
                output=temporary/mode;output.mkdir()
                text=subprocess.check_output([str(executable),str(output),mode],text=True).strip()
                first=json.loads((output/'front_readiness_first.json').read_text())
                last=json.loads((output/'front_readiness_final.json').read_text())
                assert first['phase']=='FIRST' and last['phase']=='FINAL'
                assert first['samples']==1 and last['samples']==(3 if mode=='pass' else 251)
                assert last['ready']==(mode=='pass') and last['timed_out']==(mode=='reject')
                assert first['native_tasks_submitted']==last['native_tasks_submitted']==0
                assert first['read_only'] and last['read_only']
                assert len(last['fields'])==16 and all(f['readable'] for f in last['fields'])
                assert last['elapsed_ms']==(40 if mode=='pass' else 5000)
                assert last['failure_mask']==(0 if mode=='pass' else 2)
                assert len(list(output.iterdir()))==2
                assert all(not p.name.endswith('.tmp') for p in output.iterdir())
                print(text);records.append({'test':name,'mode':mode,'result':text,'first':first,'final':last})
result={'hardware_tested':False,'predicate_checks':61,'durable_adapter_cases':2,'records':records,'source_sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'src/front_readiness.h',root/'src/front_readiness_host.inc',root/'tests/test_front_readiness.c',root/'tests/test_front_readiness_host.c']}}
destination=root/'results/front-readiness-tests.json';destination.parent.mkdir(exist_ok=True);destination.write_text(json.dumps(result,indent=2)+'\n')
print(destination)

#!/usr/bin/env python3
"""Exercise the actual menu display parser; no X, camera, or hardware calls."""
from pathlib import Path
import hashlib,json,subprocess,tempfile
R=Path(__file__).resolve().parents[1]
P=R.parents[1]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    header=R/'src/ui_state.h'
    code='''#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "HEADER"
int main(void){char b[3000];size_t n=fread(b,1,sizeof b-1,stdin);b[n]=0;struct nxl_progress_status s;memset(&s,0,sizeof s);if(!nxl_progress_status_parse(b,&s))return 3;printf("%s|%s|%s|%s\\n",s.phase,s.state,s.detail,s.time);return 0;}
'''.replace('HEADER',str(header.resolve()))
    record='NXDEMO51_PROGRESS1\nphase=INSTALL_DEPLOY\nstate=RUNNING\ndetail=Checking files\ntime=UNAVAILABLE\nend=NXDEMO51_PROGRESS_END1\n'
    cases=[('complete_record',record,True),
        ('failed_phase',record.replace('RUNNING','ERROR').replace('Checking files','INSTALL_ERROR: read-only target'),True),
        ('empty_detail',record.replace('Checking files',''),True),
        ('detail_utf8',record.replace('Checking files','安装检查'),True),
        ('missing_end',record.split('end=')[0],False),
        ('wrong_header',record.replace('NXDEMO51_PROGRESS1','BAD'),False),
        ('missing_newline',record.rstrip('\n'),False),
        ('duplicate_phase',record.replace('state=RUNNING','phase=INSTALL_ENTRY\nstate=RUNNING'),False),
        ('reordered_fields',record.replace('phase=INSTALL_DEPLOY\nstate=RUNNING','state=RUNNING\nphase=INSTALL_DEPLOY'),False),
        ('extra_tail',record+'state=COMPLETE\n',False),
        ('phase_control',record.replace('INSTALL_DEPLOY','INSTALL\tDEPLOY'),False),
        ('detail_control',record.replace('Checking files','Checking\x1bfiles'),False),
        ('oversize_phase',record.replace('INSTALL_DEPLOY','X'*49),False),
        ('oversize_state',record.replace('RUNNING','X'*49),False),
        ('oversize_detail',record.replace('Checking files','X'*1025),False),
        ('max_detail',record.replace('Checking files','X'*1024),True),
        ('empty_time',record.replace('UNAVAILABLE',''),False)]
    results=[]
    with tempfile.TemporaryDirectory(prefix='nx51-menu-progress-') as tmp:
        root=Path(tmp);(root/'test.c').write_text(code)
        subprocess.run(['cc','-O2','-Wno-unused-function',str(root/'test.c'),'-o',str(root/'test')],check=True)
        for name,text,valid in cases:
            p=subprocess.run([str(root/'test')],input=text,text=True,capture_output=True)
            assert (p.returncode==0)==valid,(name,p.returncode,p.stdout,p.stderr)
            results.append(dict(case=name,passed=True))
    menu=(R/'src/menu.c').read_text()
    assert 'kernel-restart' not in menu and 'nxl-kernel-reset' not in menu and 'action==8' not in menu
    assert 'PROGRESS / LOG' in menu and 'O_NOFOLLOW|O_NONBLOCK' in menu
    assert 'nxl_raw_action' in header.read_text() and 'CHECK REPORT' in header.read_text()
    results.append(dict(case='no_kernel_restart_dispatch_and_read_only_progress_UI',passed=True))
    out=dict(passed=True,tests_run=len(results),hardware_tested=False,X11_tested=False,
        actual_C_parser=True,cases=results,source_sha256={str(x.relative_to(P)):digest(x) for x in [header,R/'src/menu.c']})
    v=P/'validation';v.mkdir(exist_ok=True)
    (v/'menu-progress-tests.json').write_text(json.dumps(out,indent=2)+'\n')
    (v/'menu-progress-tests.txt').write_text('PASS '+str(len(results))+' actual C parser and read-only UI source cases; no X11/camera/hardware\n')
    print(json.dumps(dict(passed=True,tests_run=len(results))))
if __name__=='__main__':main()

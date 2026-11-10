#!/usr/bin/env python3
"""Run actual listener event logging with a preexisting SD logs directory."""
from pathlib import Path
import hashlib,json,subprocess,tempfile
R=Path(__file__).resolve().parents[1];P=R.parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    source=(R/'src/keys.c').read_text()
    events=source[source.index('static int debug;'):source.index('int main(')]
    results=[]
    with tempfile.TemporaryDirectory(prefix='nx51-keys-logs-') as tmp:
        w=Path(tmp);sd=w/'sdcard';sd.mkdir();alias=w/'mmc';alias.symlink_to(sd,target_is_directory=True)
        program='#include <stdio.h>\n#include <stdlib.h>\n#include <unistd.h>\n'+events.replace('/mnt/mmc/',str(alias)+'/')+'\nint main(int argc,char **argv){if(argc!=2)return 2;debug=atoi(argv[1]);event_log("LISTENER_0.2.1",115,119);event_log("INPUTS_OPEN",3,4);return 0;}\n'
        (w/'test.c').write_text(program);subprocess.run(['cc',str(w/'test.c'),'-o',str(w/'test')],check=True)
        nofolder=subprocess.run([str(w/'test'),'1'],capture_output=True,text=True,check=True)
        assert 'LISTENER_0.2.1' in nofolder.stderr and not (sd/'NXDEMO51').exists()
        results.append(dict(case='absent_log_directory_not_created_by_listener',passed=True))
        logs=sd/'NXDEMO51/logs';logs.mkdir(parents=True)
        old=sd/'nx-lut-lite-startup.log';old.write_text('OLD_ROOT_LOG_UNCHANGED\n')
        subprocess.run([str(w/'test'),'1'],capture_output=True,text=True,check=True)
        assert (logs/'startup.log').read_text()=='LISTENER_0.2.1 115 119\nINPUTS_OPEN 3 4\n'
        assert old.read_text()=='OLD_ROOT_LOG_UNCHANGED\n'
        results.append(dict(case='real_C_startup_events_append_only_inside_logs_via_SD_alias',passed=True))
        subprocess.run([str(w/'test'),'1'],capture_output=True,text=True,check=True)
        assert (logs/'startup.log').read_text().count('LISTENER_0.2.1')==2
        results.append(dict(case='later_events_preserve_prior_startup_records',passed=True))
        (logs/'startup.log').unlink();quiet=subprocess.run([str(w/'test'),'0'],capture_output=True,text=True,check=True)
        assert not quiet.stderr and not (logs/'startup.log').exists()
        results.append(dict(case='debug_off_has_no_logging_side_effect',passed=True))
    assert '"/mnt/mmc/nx-lut-lite-startup.log"' not in source
    assert '"/mnt/mmc/NXDEMO51/logs/startup.log"' in source and 'KEYS_LOGS_FOLDER_R4' in source
    results.append(dict(case='source_has_dedicated_log_path_and_version_identity',passed=True))
    rec=dict(passed=True,tests_run=len(results),actual_C_event_logger=True,hardware_tested=False,input_devices_tested=False,
        source_sha256={'base/nx-lut-lite/src/keys.c':sha(R/'src/keys.c')},cases=results)
    (P/'validation/keys-log-tests.json').write_text(json.dumps(rec,indent=2)+'\n')
    (P/'validation/keys-log-tests.txt').write_text('PASS '+str(len(results))+' actual C event logging cases; no input/camera/hardware\n')
    print(json.dumps({'passed':True,'tests_run':len(results)}))
if __name__=='__main__':main()

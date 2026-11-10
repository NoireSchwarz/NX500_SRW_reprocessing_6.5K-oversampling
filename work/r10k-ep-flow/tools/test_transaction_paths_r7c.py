"""Run the actual installer path selector in an isolated shell filesystem.

This tests name selection and preservation, not deployment, mount or rollback.
"""
from pathlib import Path
import argparse, hashlib, json, subprocess, tempfile

parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,default=Path('work/patched-installer/installer/deploy.sh.in'))
parser.add_argument('--shell',type=Path,default=Path('C:/Program Files/Git/usr/bin/sh.exe'))
parser.add_argument('--output',type=Path,default=Path('work/tests/transaction-paths.json'))
a=parser.parse_args()
text=a.source.read_text()
fragment=text[text.index('set_transaction_paths()'):text.index('transaction_suffix=$$\nset_transaction_paths\n')]
cases=['WORK','STAGE','STAGE.failed','CURRENT','BTPREV','BTNEW','METAOLD']
script='''set -eu
PATH=/usr/bin:/bin:$PATH
export PATH
task_root=$PWD
PARENT=$PWD/opt/usr
BT=$PWD/usr/sbin/bluetoothd
mkdir -p "$PARENT" "$PWD/usr/sbin" "$PWD/tmp"
'''+fragment+'''
transaction_suffix=$$
set_transaction_paths
select_transaction_paths
[ "$transaction_suffix" = "$$" ]
test_index=0
for test_leaf in WORK STAGE STAGE.failed CURRENT BTPREV BTNEW METAOLD;do
    transaction_suffix=$$
    set_transaction_paths
    case "$test_leaf" in
        WORK) test_path=$WORK;;STAGE) test_path=$STAGE;;STAGE.failed) test_path=$STAGE.failed;;
        CURRENT) test_path=$CURRENT;;BTPREV) test_path=$BTPREV;;BTNEW) test_path=$BTNEW;;METAOLD) test_path=$METAOLD;;
    esac
    printf 'KEEP_OLD_BACKUP_%s' "$test_leaf" > "$test_path"
    select_transaction_paths
    [ "$transaction_suffix" = "$$.1" ]
    [ "$(cat "$test_path")" = "KEEP_OLD_BACKUP_$test_leaf" ]
    # Test fixture cleanup is limited to the exact file it just created.
    rm "$test_path"
    test_index=$((test_index+1))
done
# All 128 candidates occupied: reject, preserving every file.
i=0
while [ "$i" -lt 128 ];do
    transaction_suffix=$$
    [ "$i" = 0 ] || transaction_suffix=$$.$i
    set_transaction_paths
    printf 'RETAIN_%s' "$i" > "$CURRENT"
    i=$((i+1))
done
if select_transaction_paths;then exit 9;fi
i=0
while [ "$i" -lt 128 ];do
    transaction_suffix=$$
    [ "$i" = 0 ] || transaction_suffix=$$.$i
    set_transaction_paths
    [ "$(cat "$CURRENT")" = "RETAIN_$i" ]
    i=$((i+1))
done
printf 'PASS 9 actual installer path-selector cases; all 128 retained backups preserved\\n'
'''
with tempfile.TemporaryDirectory(dir=a.output.parent.resolve(),prefix='selector-') as tmp:
    path=Path(tmp)/'model.sh';path.write_text(script,newline='\n')
    p=subprocess.run([str(a.shell),str(path)],cwd=tmp,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30)
    assert p.returncode==0,(p.returncode,p.stdout,p.stderr)
result={'passed':True,'cases':9,'source_sha256':hashlib.sha256(a.source.read_bytes()).hexdigest(),
        'stdout':p.stdout,'hardware_tested':False,'scope':__doc__}
a.output.write_text(json.dumps(result,indent=2)+'\n');print(p.stdout.strip())

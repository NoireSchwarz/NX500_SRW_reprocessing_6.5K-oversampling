"""Compile production probe and replay actual failed RESET samples on ARM.

The CPU model runs compiled predicates, not camera services. Live camera RESET
is still required after deployment. No native job or MMIO is executed here.
"""
from pathlib import Path
import hashlib, importlib.util, json, os, struct, subprocess, sys

R = Path(__file__).resolve().parents[1]
W = R.parent
V = R / 'validation/r7i'
V.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(W / 'tooling/python'))
spec = importlib.util.spec_from_file_location('arm_model', R / 'tools/arm_model.py')
model = importlib.util.module_from_spec(spec)
spec.loader.exec_module(model)
zig = W / 'tooling/python/ziglang/zig.exe'
runtime = W / 'tooling/arm-linux-musleabi-cross'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
rows = []

def build(source, output):
    p = subprocess.run([sys.executable, str(R / 'core/tools/build_front.py'), '--zig', str(zig),
        '--cross-runtime', str(runtime), '--source', str(source), '--output', str(output)],
        capture_output=True, text=True, encoding='utf8', errors='replace', timeout=120)
    assert p.returncode == 0, (p.stdout, p.stderr)

def arm(name, source, args=()):
    target = V / (name + '.arm')
    build(source, target)
    result = model.ArmModel(target, args).run()
    assert result['exit'] == 0, result
    rows.append({'name': name, 'source_sha256': sha(source), 'binary_sha256': sha(target), **result})
    print(result['stdout'].strip(), flush=True)

probe = R / 'core/bin/nxl-session-rearm.arm'
build(R / 'core/src/nxl-session-rearm.c', probe)
result = model.ArmModel(probe, ['--version']).run()
assert result['exit'] == 0 and 'READONLY_REARM_R7I' in result['stdout'], result
print(result['stdout'].strip(), flush=True)

# The unchanged R7h predicate must reproduce the same exact field failure.
legacy = V / 'legacy-field-replay.c'
legacy.write_text('#include <assert.h>\n#include <stdio.h>\n#include "' +
    (W / 'r7h/core/src/session_rearm_backend.h').as_posix() + '"\n#include "' +
    (R / 'core/tests/fixtures/field_rearm_r7h.h').as_posix() + '"\n' + r'''
int main(void){for(unsigned i=0;i<16;i++){struct r7_backend_sample s;memcpy(&s,field_r7h_backend[i],sizeof s);
const char *e=r7_backend_error(&s,0xa1c00000u,0xaf65c000u);assert(e&&!strcmp(e,"BACKEND_JPEG_REFERENCES_OLD_PA"));}
puts("PASS R7h failure reproduced on all 16 exact field samples");return 0;}
''', encoding='utf8', newline='\n')
arm('legacy-field-replay', legacy)
for name, source in [
    ('latched-jpeg', 'core/tests/test_rearm_latched_jpeg_r7i.c'),
    ('prior-retirement', 'core/tests/test_rearm_retirement_r7d.c'),
    ('allocation-contract', 'core/tests/test_session_rearm_contract.c'),
    ('menu-reset-status', 'core/tests/test_reset_status_r7d.c'),
]:
    arm(name, R / source)

# Preserve the comprehensive initialized/quiet backend regression. Embed only
# its existing on-disk input read, so the modeled OS needs no file service.
field = (R / 'core/tests/fixtures/r7c-reset/backend-sample-00.bin').read_bytes()
quiet = bytearray(field[16:16 + 0x554])
struct.pack_into('<I', quiet, 0x400, 3)
quiet += field[16 + 0x554:16 + 0x554 + 12]
source = (R / 'core/tests/test_session_rearm_backend.c').read_text(encoding='utf8')
prefix = 'assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f&&fread(good.kernel,1,sizeof good.kernel,f)==sizeof good.kernel&&fread(good.mutex,1,12,f)==12&&fgetc(f)==EOF&&!fclose(f));'
assert prefix in source
source = source.replace('int main(int argc,char**argv)', 'int main(void)')
source = source.replace(prefix, 'const unsigned char fixture[]={' + ','.join(str(x) for x in quiet) + '};memcpy(good.kernel,fixture,sizeof good.kernel);memcpy(good.mutex,fixture+sizeof good.kernel,12);')
generated = V / 'quiet-backend-embedded.c'
generated.write_text(source, encoding='utf8', newline='\n')
arm('quiet-backend-regression', generated)

# Native output evidence: the completed JPEG task restores prior addresses,
# including addresses inside the old frontend allocation. They are ordinary
# configuration values, not a truthful allocation-ownership flag.
steps_path = W / 'field-r7f-complete/jpeg/attempt_1/hardware/01_jpeg/steps.bin'
steps = steps_path.read_bytes()
count = struct.unpack_from('<I', steps, 8)[0]
records = [struct.unpack_from('<26I', steps, 12 + i * 104) for i in range(count)]
entry = next(x for x in records if x[0] == 101)
cleanup = next(x for x in records if x[0] == 999)
assert entry[7:15] == cleanup[7:15]
assert any(0xa1c00000 <= p < 0xa9c00000 for p in cleanup[12:15])
evidence = dict(steps_sha256=sha(steps_path), entry_step=101, cleanup_step=999,
    prior_configuration_restored=True, restored_address_hex=[hex(x) for x in cleanup[12:15]])

validation = dict(format='NX51_R7I_LATCHED_JPEG_RETIREMENT_VALIDATION1', passed=True,
    hardware_tested=False, source_sha256=sha(R / 'core/src/nxl-session-rearm.c'),
    backend_contract_sha256=sha(R / 'core/src/session_rearm_backend.h'),
    probe_sha256=sha(probe), probe_bytes=probe.stat().st_size,
    exact_failed_field_samples=16, native_cleanup_evidence=evidence, checks=rows,
    scope='Actual production retirement predicates execute as compiled ARM with modeled stdio/memory/clock only. Original R7h failure replayed; allocator, engine activity, queues, IRQ, live DMA and stability regressions remain. No camera IO or native job.',
    camera_reset_pending=True)
(V / 'validation.json').write_text(json.dumps(validation, indent=2) + '\n', encoding='utf8')
print(json.dumps({'passed': True, 'ARM_cases': len(rows), 'probe_bytes': validation['probe_bytes']}), flush=True)

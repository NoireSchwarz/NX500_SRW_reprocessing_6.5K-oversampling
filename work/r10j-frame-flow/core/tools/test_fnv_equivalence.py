from pathlib import Path
import json,sys,subprocess,hashlib,os,ctypes,ctypes.util,struct
S=Path(__file__).resolve().parents[1];W=S.parents[1];B=S/'build/fnv-r6';B.mkdir(parents=True,exist_ok=True);V=S/'results';V.mkdir(exist_ok=True);Q=W/'demo-p49/tooling/qemu/usr/bin/qemu-arm-static';Z=W/'wb-load-p42/deps/ziglang/zig';X=W/'wb-load-p42/cross-runtime/arm-linux-musleabi-cross';sys.path.insert(0,str(W/'wb-load-p42/deps'));from elftools.elf.elffile import ELFFile
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
with (S/'build/rtos_front.elf').open('rb') as f:
 e=ELFFile(f);sym=next(x for x in e.get_section_by_name('.symtab').iter_symbols() if x.name=='hash');sec=e.get_section(sym['st_shndx']);off=sym['st_value']-sec['sh_addr'];raw=sec.data()[off:off+sym['st_size']];address=sym['st_value'];assert raw and len(raw)%4==0
blob=B/'actual-rtos-hash.bin';blob.write_bytes(raw)
# Disassemble final RTOS instructions, not a separately optimized C model.
l=ctypes.CDLL(ctypes.util.find_library('LLVM'))
for n in ['LLVMInitializeARMTargetInfo','LLVMInitializeARMTarget','LLVMInitializeARMTargetMC','LLVMInitializeARMAsmPrinter','LLVMInitializeARMDisassembler']:getattr(l,n)()
l.LLVMCreateDisasm.argtypes=[ctypes.c_char_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p,ctypes.c_void_p];l.LLVMCreateDisasm.restype=ctypes.c_void_p;l.LLVMDisasmInstruction.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint64,ctypes.c_uint64,ctypes.c_char_p,ctypes.c_size_t];l.LLVMDisasmInstruction.restype=ctypes.c_size_t;dc=l.LLVMCreateDisasm(b'armv7-none-eabi',None,0,None,None);data=ctypes.create_string_buffer(raw);buf=ctypes.create_string_buffer(512);lines=[]
for off in range(0,len(raw),4):
 l.LLVMDisasmInstruction(dc,ctypes.byref(data,off),len(raw)-off,address+off,buf,512);text=buf.value.decode();assert '\tbl\t' not in text and '\tblx\t' not in text;lines.append(f'{address+off:08x} {raw[off:off+4].hex()} {text}')
asm='\n'.join(lines)+'\n';assert '\tldr\t' in asm and '\tldrb\t' in asm;(V/'fnv-r6-disassembly.txt').write_text(asm)
test=B/'test_fnv_uncached.c';test.write_text((S/'tests/test_fnv_uncached.c').read_text().replace('RTOS_HASH_BYTES_PLACEHOLDER',str(blob)))
host=B/'test_fnv.host';subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-I',str(S/'src'),str(test),'-o',str(host)],check=True)
# Rebuild the real production streaming stage for the pinned0183 fixture.
stage=B/'0183-stage.bin';decoder=B/'test-real-source';decode_report=B/'decoder';decode_report.mkdir(exist_ok=True)
for stale in decode_report.iterdir():stale.unlink()
wrapper=B/'real-source-wrapper.c';wrapper.write_text('#include <unistd.h>\n#include '+json.dumps(str(S/'tests/test_real_source.c'))+'\n__attribute__((constructor)) static void select_case(void){assert(p48_select_case("0"));}\n')
subprocess.run(['cc','-O2','-Wno-unused-function','-Wno-misleading-indentation','-I',str(S/'src'),'-include',str(S/'src/p48_case.h'),str(wrapper),'-o',str(decoder)],check=True)
subprocess.run([str(decoder),str(S.parent/'assets/SAM_0183.SRW'),str(S.parent/'assets/SAM_0183.rows.bin'),str(decode_report),str(stage)],check=True,capture_output=True);assert sha(stage)=='846a5874e057bf9b264d8d3a7b0372145f9f31cfd5d782edcbd85e91605db3a9'
F=W/'demo51-field-success/JOB_1793869199_415_10dc3936/front_2/front/case_00';target=B/'real-target.bin';target.write_bytes((F/'p39_output.y8').read_bytes()+(F/'p39_plane_gap.bin').read_bytes()+(F/'p39_output.uv8').read_bytes());assert target.stat().st_size==56177664
arm=B/'test_fnv.arm';subprocess.run([sys.executable,str(S/'tools/build_front.py'),'--zig',str(Z),'--cross-runtime',str(X),'--source',str(test),'--output',str(arm)],check=True,capture_output=True)
cases=[]
for runtime,prefix in [('HOST',[str(host)]),('actual_ARM_QEMU',[str(Q),str(arm)])]:
 p=subprocess.run(prefix+[str(stage),str(target)],capture_output=True,text=True,check=True,timeout=180);d=json.loads(p.stdout);assert d['passed'];d['runtime']=runtime;cases.append(d);print(runtime,p.stdout.strip(),flush=True)
report=dict(format='NX500_R6_EXACT_FNV_EQUIVALENCE1',passed=True,hardware_tested=False,actual_camera_timing_measured=False,cases=cases,actual_production_hash_extracted=True,hash_symbol_address=address,hash_symbol_bytes=len(raw),hash_symbol_sha256=sha(blob),rtos_elf_sha256=sha(S/'build/rtos_front.elf'),rtos_source_sha256=sha(S/'src/rtos_front.c'),helper_sha256=sha(S/'src/fnv_uncached.h'),test_source_sha256=sha(S/'tests/test_fnv_uncached.c'),stage_sha256=sha(stage),target_sha256=sha(target),host_binary_sha256=sha(host),arm_binary_sha256=sha(arm),disassembly_assertions=['Final ELF hash contains aligned LDR word load and byte prefix/tail loads.','Final hash has no external BL/BLX dependency, allowing exact instruction replay in Linux ARM process.'],limitations=['QEMU memory is ordinary private RAM, not camera Normal-NC RAM; these tests prove equivalence and boundary behavior, not speed.','No completion timeout, cache contract, byte extent, callback or firmware code was changed.'])
(V/'fnv-r6-equivalence.json').write_text(json.dumps(report,indent=2)+'\n')

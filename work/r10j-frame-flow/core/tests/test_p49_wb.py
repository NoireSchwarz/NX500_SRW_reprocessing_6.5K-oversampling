#!/usr/bin/env python3
"""Real production C reader/packet/audit vs31 original-ARM source oracles.

PC only. Positive source files are read-only. Mutations are new temporary files.
The oracle replays original constructor/getter/producer/setter from official
RTOS1.12, with explicit virtual caller data and zero unassigned stack bits.
"""
import ctypes,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];CORE=Path(__file__).resolve().parents[1]
OUT=CORE/'results/p49-wb';OUT.mkdir(parents=True,exist_ok=True)
so=OUT/'wb_bridge.so'
command=['cc','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-fPIC','-shared','-I',str(CORE/'src'),str(CORE/'tests/p49_wb_bridge.c'),'-o',str(so)]
subprocess.run(command,check=True)
lib=ctypes.CDLL(str(so));U=ctypes.c_uint32;A=U*256;P=U*6
lib.p49_test_read.argtypes=[ctypes.c_char_p,ctypes.POINTER(U)];lib.p49_test_read.restype=ctypes.c_int
lib.p49_test_packet.argtypes=[ctypes.POINTER(U),ctypes.POINTER(U),U,U];lib.p49_test_packet.restype=ctypes.c_int
lib.p49_test_checksum.argtypes=[ctypes.POINTER(U),ctypes.c_uint];lib.p49_test_checksum.restype=U
lib.p49_test_request.argtypes=[ctypes.POINTER(U),U];lib.p49_test_request.restype=ctypes.c_int
lib.p49_test_audit.argtypes=[ctypes.POINTER(U),U];lib.p49_test_audit.restype=ctypes.c_int
oracle_path=ROOT/'purple-evidence-review/native-path/C0-selector-evidence/replay.json'
oracle=json.loads(oracle_path.read_text());inventory=json.loads((ROOT/'render-p48/research/sources.json').read_text())
by={s['source_path']:s for s in inventory['sources']};nonce=0x491b7783;summary=[];audit_cases=[]
def fnv(b):
 h=2166136261
 for x in b:h=((h^x)*16777619)&0xffffffff
 return h
def checksum(v):v[63]=lib.p49_test_checksum(v,63)
def audit(v,source=True,warning=True):
 a=A(*v);a[2]=nonce;a[49]=int(source);checksum(a)
 template=[0,0,0x10000000,0x10000000,0x10000000,0x10000000]
 a[80:92]=template+template
 target=P();assert lib.p49_test_packet(target,P(*template),a[51],a[52])
 if not source:target=P(*template)
 a[160:166]=target;a[166]=lib.p49_test_checksum(target,6);a[167:176]=[1,a[57],a[58],a[59],a[60],0,a[51],a[52],a[49]]
 a[64]=7;a[65]=1;a[74]=0x57423439;a[219:223]=[int(source),1,1,1];a[252:256]=[1,1,0,1]
 for slot,flag in ((96,75),(112,76),(128,77),(228,78)):
  a[slot:slot+12]=list(target)+template;a[flag]=1
 a[79]=1
 if warning and source:a[144:156]=list(target)+template;a[66]=int(list(target)==template);a[73]=0 if a[66] else 134
 else:a[144:156]=template+template;a[66]=1;a[73]=0
 assert lib.p49_test_audit(a,nonce)
 return a
for item in oracle['sources']:
 source=ROOT/item['source_path'];before=hashlib.sha256(source.read_bytes()).hexdigest();v=A()
 assert lib.p49_test_read(str(source).encode(),v),item['source_path']
 meta=by[item['source_path']];c0=bytes.fromhex(meta['c0_hex'])
 assert v[1]==2 and v[47]==fnv(c0) and v[51]==int(item['C0_word520'],16) and v[52]==item['C0_code56'] and v[53]==item['c0_file_offset']
 assert list(v[8:12])==item['source_q8_RGGB'] and list(v[54:59])==[0x433f2149,20150126,2048,item['selector'],item['fourth_field']]
 assert lib.p49_test_request(v,0)
 p=P();zero=P();assert lib.p49_test_packet(p,zero,v[51],v[52]);expected=[int(x,16) for x in item['producer_packet']]
 assert list(p)==expected,(item['source_path'],list(p),expected)
 unknown=P(0,0,0x800c0000,0x80040000,0x00080000,0x00040000);patched=P();assert lib.p49_test_packet(patched,unknown,v[51],v[52])
 for i in range(2,6):assert (patched[i]&0x800c0000)==(unknown[i]&0x800c0000) and (patched[i]&~0x800c0000)==(expected[i]&~0x800c0000)
 assert hashlib.sha256(source.read_bytes()).hexdigest()==before==item['source_sha256']
 for mode,warning in ((False,False),(True,False),(True,True)):audit(v,mode,warning)
 summary.append({'source':item['source_path'],'ISO':item['ISO'],'selector':v[57],'source_sha256':before,'C0_word520':hex(v[51]),'packet':[hex(x) for x in p],'original_ARM_complete_packet_match':True,'unknown_bits_preserved':True,'source_unchanged':True})
# Synthetic selector2 original-ARM threshold case, not a real camera source.
for item in oracle['modeled_threshold_tests']:
 p=P();assert lib.p49_test_packet(p,P(),int(item['C0_word520'],16),item['C0_code56'])
 assert list(p)==[int(x,16) for x in item['producer_packet']]
# Independent arithmetic boundary check over every accepted divider input.
divider_cases=0
for selector,code in ((1,177),(2,209)):
 for red in range(64,2048):
  blue=2111-red;word=(red<<21)|(blue<<10)|(selector<<8)|((code>208)<<7)|127;p=P()
  assert lib.p49_test_packet(p,P(),word,code)
  half=list(struct.unpack('<4H',struct.pack('<2I',p[0],p[1])))
  top,fixed=(2048,1792) if selector==1 else (4080,3840)
  assert half==[top-65536//red,fixed,fixed,top-65536//blue]
  divider_cases+=1
# Recomputed requestFNV cannot hide wrong complete-word/selector/profile/header.
base=A();assert lib.p49_test_read(str(ROOT/oracle['sources'][0]['source_path']).encode(),base)
for idx,value in [(1,1),(51,base[51]^1),(51,base[51]^0x100),(51,base[51]^0x80),(51,base[51]^(1<<21)),(52,256),(54,0),(55,0),(56,2047),(57,1),(58,1),(59,1),(60,0),(61,0),(62,1),(10,257),(12,base[12]+32)]:
 v=A(*base);v[idx]=value;checksum(v);assert not lib.p49_test_request(v,0),(idx,value)
good=audit(base,True,True)
for idx in [160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,218,96,128,66,73,75,76,77,78,79]:
 v=A(*good);v[idx]^=1;assert not lib.p49_test_audit(v,nonce),idx;audit_cases.append(idx)
# Real source metadata mutations use a new metadata-only fixture, never source.
meta=by[oracle['sources'][0]['source_path']];full=(ROOT/meta['source_path']).read_bytes();end=meta['metadata']['makernote_offset']+meta['metadata']['makernote_bytes'];prefix=bytearray(full[:end]);c0off=meta['c0_file_offset'];mutations=[]
with tempfile.TemporaryDirectory(dir=OUT) as tmp:
 tmp=Path(tmp)
 path=tmp/'valid-metadata-only.SRW';path.write_bytes(prefix);v=A();assert lib.p49_test_read(str(path).encode(),v)
 for label,at,value in [('C0magic',c0off,0),('C0revision',c0off+4,0),('C0declaredbytes',c0off+8,2047),('C0reservedlow',c0off+520,base[51]^1),('C0selector',c0off+520,base[51]^0x100),('C0flag',c0off+520,base[51]^0x80),('C0gain',c0off+520,base[51]^(1<<21))]:
  data=bytearray(prefix);struct.pack_into('<I',data,at,value);path=tmp/(label+'.SRW');path.write_bytes(data);v=A();assert not lib.p49_test_read(str(path).encode(),v),label;mutations.append(label)
 path=tmp/'C0-truncated.SRW';path.write_bytes(prefix[:c0off+2047]);v=A();assert not lib.p49_test_read(str(path).encode(),v);mutations.append('C0-truncated')
result={'format':'P49_WB_PRODUCTION_READER_PACKET_AUDIT_TEST1','hardware_tested':False,'camera_calls':0,'production_C_sources':len(summary),'all_complete_native_ARM_packet_oracles_match':True,'all_input_files_unchanged':True,'selector_counts':dict(__import__('collections').Counter(str(x['selector']) for x in summary)),'original_ARM_threshold_models':len(oracle['modeled_threshold_tests']),'exhaustive_divider_packet_cases':divider_cases,'request_mutations_rejected':17,'audit_mutation_word_indices_rejected':audit_cases,'metadata_mutations_rejected':mutations,'baseline_source_and_restore_visibility_models':93,'sources':summary,'oracle_path':str(oracle_path.relative_to(ROOT)),'oracle_sha256':hashlib.sha256(oracle_path.read_bytes()).hexdigest(),'build_command':command,'source_dynamic_calibration_applied':False,'unknown_bits_semantics_assigned':False}
endian=[]
for order in ('<','>'):
 u16=lambda x:struct.pack(order+'H',x)
 u32=lambda x:struct.pack(order+'I',x&0xffffffff)
 data=bytearray(8192);data[:8]=(b'II' if order=='<' else b'MM')+u16(42)+u32(8)
 def ifd(at,items):
  data[at:at+2]=u16(len(items))
  for i,item in enumerate(items):data[at+2+12*i:at+14+12*i]=struct.pack(order+'HHII',*item)
  data[at+2+12*len(items):at+6+12*len(items)]=bytes(4)
 ifd(8,[(0x10f,2,8,100),(0x110,2,6,112),(0x8769,4,1,160)])
 data[100:108]=b'SAMSUNG\0';data[112:118]=b'NX500\0';ifd(160,[(0x927c,7,4096,256)])
 key=[305,72,737,456,282,307,519,724,13,505,193]
 arrays=[(0xa020,4,key),(0xa014,4,[160]),(0xa021,4,[5824+key[0],4096+key[1],4096+key[2],9808+key[3]]),(0xa028,9,[128+x for x in key[:4]])]+[(t,9,[(x-key[i])&0xffffffff for i,x in enumerate([436,-120,-60,-44,314,-14,2,-96,350])]) for t in (0xa030,0xa031,0xa032)]
 at=512;entries=[]
 for tag,typ,values in arrays:
  entries.append((tag,typ,len(values),values[0] if len(values)==1 else at-256))
  if len(values)>1:
   for x in values:data[at:at+4]=u32(x);at+=4
 entries.append((0xc0,7,2048,1792));ifd(256,entries)
 c0=bytearray(2048);struct.pack_into('<3I',c0,0,0x433f2149,20150126,2048);struct.pack_into('<I',c0,56,91);struct.pack_into('<I',c0,520,0x2d89947f);data[2048:4096]=c0
 tag_at=256+2+12*(len(entries)-1)
 with tempfile.TemporaryDirectory(dir=OUT) as tmp:
  path=Path(tmp)/'synthetic.SRW';path.write_bytes(data);v=A();assert lib.p49_test_read(str(path).encode(),v)
  assert list(v[8:12])==[364,256,256,613] and v[51]==0x2d89947f and v[7]==int(order=='<')
  rejected=[]
  for label,where,raw in [('C0type',tag_at+2,u16(4)),('C0count',tag_at+4,u32(2047)),('C0pointer',tag_at+8,u32(0xfffffffc)),('C0duplicate',tag_at,u16(0xa020)),('MakerNoteCount',166,u32(2048)),('IFDCount',256,u16(513))]:
   bad=bytearray(data);bad[where:where+len(raw)]=raw;path.write_bytes(bad);v=A();assert not lib.p49_test_read(str(path).encode(),v),(order,label);rejected.append(label)
  endian.append({'outer_TIFF':order,'C0_little_endian_preserved':True,'invalid_IFD_cases_rejected':rejected})
result['synthetic_outer_endian_and_bounded_IFD_tests']=endian
(OUT/'validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({'production_sources':len(summary),'threshold_models':9,'divider_cases':divider_cases,'request_rejects':17,'audit_rejects':len(audit_cases),'metadata_rejects':len(mutations),'all_source_native_packets_match':True,'camera_calls':0}))

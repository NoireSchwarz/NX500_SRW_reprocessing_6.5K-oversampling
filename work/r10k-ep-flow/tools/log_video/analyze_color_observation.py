"""Decode R11l read-only snapshots on the PC; do not fit a camera correction.

Repeated equality is observational consistency, not atomic request association.
"""
from pathlib import Path
import ctypes as C,struct,json,hashlib,argparse,collections
U=C.c_uint32
def array(n):return U*n
class Channel(C.LittleEndianStructure):
 _fields_=[(k,U)for k in ['status','parent','gamma','color','context']]+[(k,array(n))for k,n in [('parent_head',6),('parent_gamma',1),('gamma_head',8),('color_head',8),('gamma_widths',5),('color_widths',5)]]+[('gamma_descriptor',U),('color_descriptor',U)]+[(k,array(n))for k,n in [('gamma_tail',78),('color_tail',31),('input',49)]]
class Payload(C.LittleEndianStructure):
 _fields_=[(k,array(n))for k,n in [('manager',5),('roots',4),('mapping',3),('driver',2),('callbacks',4),('dynamic',9),('binding_raw',24)]]+[('channel',Channel*2),('color_shadow',(U*10)*2),('gamma_shadow',(U*108)*2)]
class Record(C.LittleEndianStructure):
 _fields_=[(k,U)for k in ['magic','schema','bytes','index','time_lo','time_hi','duration_us','status','media_busy']]+[('first',Payload),('second',Payload),('camera_pid',U),('media_name',C.c_char*128),('checksum',U),('commit',U)]
def fnv(data):
 h=2166136261
 for v in data:h=((h^v)*16777619)&0xffffffff
 return h
def signed11(v):v&=2047;return v-2048 if v&1024 else v
def cc(words):
 half=struct.unpack('<20H',struct.pack('<10I',*words));return [[signed11(x)for x in half[g*10:g*10+9]]for g in range(2)]
def gamma(words):
 raw=struct.pack('<108I',*words);out=[]
 for branch in range(2):
  q=struct.unpack_from('<108H',raw,branch*216);out.append(dict(x14=[x&32767 for x in q[3:22]],y10_signed=[signed11(x)for x in q[25:44]]))
 return out
def channel(c):
 raw=struct.pack('<31I',*c.color_tail)
 regions=[('RGB_source_low',0,9),('RGB_source_high',18,9),('R2Y_source',36,9),('SE_source',54,4),('CC_cache_low',62,9),('CC_cache_high',80,9),('R2Y_cache',98,9),('SE_cache',116,4)]
 return dict(status=c.status,parent=c.parent,gamma=c.gamma,color=c.color,context=c.context,parent_head=list(c.parent_head),parent_gamma=list(c.parent_gamma),gamma_head=list(c.gamma_head),color_head=list(c.color_head),gamma_widths=list(c.gamma_widths),color_widths=list(c.color_widths),gamma_descriptor_raw=c.gamma_descriptor,color_descriptor_raw=c.color_descriptor,descriptors_are_packed_values_not_pointers=True,source_and_cache_raw_hex=raw.hex(),source_and_cache_s16={name:list(struct.unpack_from('<'+str(n)+'h',raw,offset))for name,offset,n in regions},gamma_object_tail_raw=list(c.gamma_tail),gamma_native_object_source=dict(x=list(c.gamma_tail)[1:20],y_raw=list(c.gamma_tail)[20:39]),gamma_native_object_cache=dict(x=list(c.gamma_tail)[40:59],y_raw=list(c.gamma_tail)[59:78]),context_raw=list(c.input))
def payload(p):return dict(manager=list(p.manager),roots=list(p.roots),mapping=list(p.mapping),driver_and_preset=list(p.driver),callbacks=list(p.callbacks),dynamic_six_value_sources_s16=list(struct.unpack('<18h',struct.pack('<9I',*p.dynamic))),native_binding_raw=list(p.binding_raw),native_binding_interpreted_as_request=False,banks=[dict(channel=channel(c),CC_submission_shadow_raw=list(p.color_shadow[i]),CC_submission_shadow_s11=cc(p.color_shadow[i]),Gamma_submission_shadow_raw=list(p.gamma_shadow[i]),Gamma_submission_curves=gamma(p.gamma_shadow[i]))for i,c in enumerate(p.channel)])
def parse_blob(blob):
 size=C.sizeof(Record)
 if len(blob)%size:raise ValueError('Partial record: preserve file; do not infer missing data')
 rows=[];previous=0
 for i in range(len(blob)//size):
  raw=blob[i*size:(i+1)*size];r=Record.from_buffer_copy(raw)
  if r.magic!=0x314c4f43 or r.schema!=1 or r.bytes!=size or r.index!=i or not 1<=r.status<=7:raise ValueError('Invalid schema/index/status at '+str(i))
  if fnv(raw[:-8])!=r.checksum or r.commit!=(0x314c4f43^r.checksum):raise ValueError('Checksum/commit mismatch at '+str(i))
  time=r.time_lo+(r.time_hi<<32)
  if i and time<previous:raise ValueError('Nonmonotonic sample time')
  previous=time
  if r.status==1 and bytes(r.first)!=bytes(r.second):raise ValueError('Inconsistent payload marked stable')
  name=bytes(r.media_name).split(b'\0')[0].decode('utf8',errors='replace')
  rows.append(dict(index=i,time_ns=time,read_duration_us=r.duration_us,status=r.status,stable_bank0=r.status==1,media_busy=r.media_busy if r.media_busy<0x80000000 else r.media_busy-0x100000000,camera_pid=r.camera_pid,media_name=name,first=payload(r.first),second=payload(r.second)if r.status!=1 else None))
 counts=collections.Counter(r['status']for r in rows);clips={}
 for r in rows:
  if not r['media_name']:continue
  c=clips.setdefault(r['media_name'],dict(samples=0,stable_samples=0,first_ns=r['time_ns'],last_ns=0,callback_sets=[]));c['samples']+=1;c['stable_samples']+=int(r['stable_bank0']);c['last_ns']=r['time_ns'];callbacks=r['first']['callbacks']
  if callbacks not in c['callback_sets']:c['callback_sets'].append(callbacks)
 return dict(format='NXL_COLOR_R11L_DECODE1',record_bytes=size,records=len(rows),status_counts=dict(counts),clips=clips,software_source_cache_submission_only=True,atomic_same_request_proven=False,hardware_consumption_proven=False,absolute_color_calibration_proven=False,correction_ready=False,samples=rows)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args();result=parse_blob(a.input.read_bytes());result['source']=dict(path=str(a.input.resolve()),sha256=hashlib.sha256(a.input.read_bytes()).hexdigest());a.out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print(json.dumps({k:v for k,v in result.items()if k!='samples'},ensure_ascii=False,indent=2))

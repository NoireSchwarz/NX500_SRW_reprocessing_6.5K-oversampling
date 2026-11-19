"""R8m passive native BayerStill Gamma evidence; read-only camera inputs.
Usage: python compare_native_gamma.py CARD JOB OUTPUT
An active single-photo timeline candidate is not proof of a call or ISP DMA.
"""
from pathlib import Path
import argparse,hashlib,json,struct
from compare_selected_photo import compare,frames,media_path,fnv,STAGES
GC_BYTES=1328
DDR_OFFSET=0x1228
DDR_BYTES=432
STATUS={0:'NOT_READ',1:'REPEATED_DATA_AND_BRACKETS_EQUAL',2:'UNCONSTRUCTED_OR_UNKNOWN_TYPE',3:'READ_UNAVAILABLE',4:'CHANGED_DURING_READ',5:'OUTSIDE_ORDINARY_RAM',6:'UNSUPPORTED_PARAMETER_SHAPE_OR_BANK_MAPPING'}
def s32(n):return n-0x100000000 if n&0x80000000 else n
def s11(n):return (n&2047)-2048 if n&1024 else n&2047
def parameters(v):
 return dict(raw=v,curve_index=(v>>29)&7,field1=(v>>24)&31,field2=(v>>20)&15,x_scale=(v>>7)&8191,custom_toggle=(v>>5)&3)
def decode_gc(b):
 if len(b)!=GC_BYTES:raise ValueError('Gamma record extent')
 v=struct.unpack('<332I',b)
 if v[:2]!=(0x3143474e,1)or v[3]!=GC_BYTES or v[-2]!=fnv(b[:-8])or v[-1]!=(v[0]^v[-2]):raise ValueError('Gamma record identity/checksum')
 if v[2]>6:raise ValueError('Gamma status')
 channels=[]
 for k in range(2):
  q=v[22+154*k:22+154*(k+1)]
  if q[0]>6 or q[1]>15:raise ValueError('Gamma channel status/mask')
  tail=q[32:110];inp=q[110:154]
  good=v[2]==1 and q[0]==1 and q[1]&7==7
  if good:
   if q[5]!=0x8062f5f8 or(q[6]>>8)&255!=k or q[11]!=0x8062f6b0 or q[14]!=q[2]or q[18]!=5 or q[27:32]!=(3,5,4,13,2):raise ValueError('Gamma type/shape guard')
   if q[5:8]!=q[8:11]or q[11:19]!=q[19:27]:raise ValueError('Gamma inconsistent brackets')
  channels.append(dict(bank=k,status=q[0],status_text=STATUS[q[0]],mask=q[1],qualified=good,parent=hex(q[2]),gamma=hex(q[3]),context=hex(q[4]),parent_brackets=[list(q[5:8]),list(q[8:11])],head_brackets=[list(q[11:19]),list(q[19:27])],parameter_widths=list(q[27:32]),current_parameters=parameters(q[12]),effective_parameters=parameters(tail[0]),effective_x19=[s32(n)for n in tail[1:20]],effective_y19=[s32(n)for n in tail[20:39]],custom_toggle=tail[39],custom_x19=[s32(n)for n in tail[40:59]],custom_y19=[s32(n)for n in tail[59:78]],input_metadata_u32=list(inp)if q[1]&8 else None,input_flags=(inp[0]&255)if q[1]&8 else None))
 if v[2]==1 and(v[4]!=0x8062e398 or v[4:9]!=v[9:14]or v[14]!=0x80592640 or v[14:18]!=v[18:22]):raise ValueError('Gamma root brackets')
 return dict(status=v[2],status_text=STATUS[v[2]],manager_before=list(v[4:9]),manager_after=list(v[9:14]),roots_before=list(v[14:18]),roots_after=list(v[18:22]),channels=channels,sha256=hashlib.sha256(b).hexdigest(),atomic=False,hardware_consumption_proven=False)
def packed(bank):
 if len(bank)!=1472:raise ValueError('parameter bank extent')
 b=struct.pack('<1472I',*bank)[DDR_OFFSET:DDR_OFFSET+DDR_BYTES]
 groups=[]
 for at,label in [(0,'after_803887f0_y_remap'),(216,'before_803887f0_y_remap')]:
  q=struct.unpack_from('<108H',b,at)
  groups.append(dict(label=label,offset=at,raw_u16=list(q),node_x19=[x&32767 for x in q[3:22]],node_y19=[s11(y)for y in q[25:44]],node_fields_are_packed_not_SRW_11_nodes=True))
 return dict(ddr_bank_offset=DDR_OFFSET,bytes=DDR_BYTES,sha256=hashlib.sha256(b).hexdigest(),hex=b.hex(),groups=groups)
def verify_source(s,source_hash):
 if s.get('source_sha256')!=source_hash or s.get('schema')!=1 or s.get('observation_only')is not True or s.get('curve_applied')is not False:raise ValueError('tone source identity/mode')
 if s.get('status')!=1:return False
 key=s['key'];curves=s['curves']
 if len(key)!=11 or len(curves)!=2 or any(not isinstance(n,int)or not 0<=n<=0xffffffff for n in key):raise ValueError('tone key/curve shape')
 for k,c in enumerate(curves):
  if c['tag']!=['A040','A042'][k]or len(c['raw'])!=23 or c['raw'][0]!=11 or len(c['x'])!=11 or len(c['y'])!=11:raise ValueError('tone curve shape')
  if any(not isinstance(n,int)or not 0<=n<=0xffffffff for n in c['raw']):raise ValueError('tone raw words')
  if c['x']!=[(c['raw'][1+i]+key[i])&0xffffffff for i in range(11)]or c['y']!=[(c['raw'][12+i]-key[i])&0xffffffff for i in range(11)]:raise ValueError('tone arithmetic')
  if any(not 0<=n<=16383 for n in c['x'])or any(not 0<=n<=255 for n in c['y'])or any(a>=b for a,b in zip(c['x'],c['x'][1:])):raise ValueError('tone node range')
  if not s['maker']<=c['offset'] or c['offset']+92>s['maker_end']:raise ValueError('tone source bounds')
 return True
def run(card,job,out):
 out=Path(out);out.mkdir(parents=True,exist_ok=True);base=compare(card,job,out/'configuration');source=base['source'];front=next(Path(job).glob('front_*/front/selected-source.json')).parent
 tone_path=front/'source-tone-curves.json';tone=json.loads(tone_path.read_text('utf8'))if tone_path.is_file()else None
 tone_ok=verify_source(tone,source['source_sha256'])if tone else False
 result=dict(schema=1,release='R8m',job=Path(job).name,source_sha256=source['source_sha256'],source_tone_curves=tone,source_tone_decoded=tone_ok,native_pairing_status=base['pairing_status'],pairing_is_timeline_inference=True,source_A040_A042_to_native19_mapping_proven=False,source_tone_curve_override_added=False,hardware_consumption_proven=False,native_gamma_metadata=[],native_active_blocks=[],module_boundaries=[])
 for i,r in enumerate(base['module_stages']):
  result['module_boundaries'].append(dict(stage=STAGES[i],qualified=r['stable']and r['mask']==31,roots=r['roots_before'],banks=[packed(b)for b in r['banks']]))
 for r in base.get('native_candidates',[]):
  result['native_active_blocks'].append(dict(begin_ns=r['begin_ns'],end_ns=r['end_ns'],roots=r['roots_before'],parameter_bases_verified=False,banks=[packed(b)for b in r['banks']]))
 if base.get('photo_event')and base['pairing_status']=='SINGLE_PHOTO_TIMELINE_CANDIDATE':
  link=json.loads((front/'native-recorder-link.json').read_text('utf8'));rec=media_path(Path(card),link['recorder_report']);end=base['photo_event']['mono_ns'];begin=max(0,end-8*10**9)
  for h,p in frames(rec/'contexts.bin'):
   if h['kind']!=14 or not begin<=h['begin_ns']<=end or h['end_ns']>=link['selection_mono_ns']:continue
   r=decode_gc(p);r.update(h)
   if 3<=r['roots_before'][1]&255<=6:
    for b in result['native_active_blocks']:
     if r['status']==1 and abs(b['begin_ns']-h['begin_ns'])<=50_000_000 and b['roots']==r['roots_before']:b['parameter_bases_verified']=True
    for c in r['channels']:
     c['nearby_packed_matches']=[]
     if not c['qualified']:continue
     for j,b in enumerate(result['native_active_blocks']):
      if abs(b['begin_ns']-h['begin_ns'])>50_000_000 or b['roots']!=r['roots_before']:continue
      g=b['banks'][c['bank']]['groups'][1]
      c['nearby_packed_matches'].append(dict(native_block=j,delta_ns=b['begin_ns']-h['begin_ns'],x19_equal=g['node_x19']==c['effective_x19'],y19_equal=g['node_y19']==c['effective_y19'],association_is_timeline_inference=True))
    result['native_gamma_metadata'].append(r)
 if result['native_active_blocks']:
  # All variants are retained above. This is the same last-active timeline
  # candidate used by compare_selected_photo, not an exact adoption point.
  n=result['native_active_blocks'][-1];result['native_vs_module']=[]
  for m in result['module_boundaries']:
   if not m['qualified']:continue
   result['native_vs_module'].append(dict(stage=m['stage'],native_parameter_bases_verified=n['parameter_bases_verified'],bank_differences=[dict(bank=k,packed_equal=n['banks'][k]['sha256']==m['banks'][k]['sha256'],different_u16=sum(a!=b for ga,gb in zip(n['banks'][k]['groups'],m['banks'][k]['groups'])for a,b in zip(ga['raw_u16'],gb['raw_u16'])),node_x19_equal=[a['node_x19']==b['node_x19']for a,b in zip(n['banks'][k]['groups'],m['banks'][k]['groups'])],node_y19_equal=[a['node_y19']==b['node_y19']for a,b in zip(n['banks'][k]['groups'],m['banks'][k]['groups'])],raw_changes=[dict(group=g,offset=g*216+i*2,native=a,module=b,xor=a^b,inside_known_node_field=((a^b)&32767)!=0 if 3<=i<22 else((a^b)&2047)!=0 if 25<=i<44 else None)for g,(ga,gb)in enumerate(zip(n['banks'][k]['groups'],m['banks'][k]['groups']))for i,(a,b)in enumerate(zip(ga['raw_u16'],gb['raw_u16']))if a!=b])for k in range(2)]))
 result['native_gamma_status']='ACTIVE_METADATA_CAPTURED'if any(c['qualified']for r in result['native_gamma_metadata']for c in r['channels'])else'NO_QUALIFIED_ACTIVE_GAMMA_METADATA'
 result['native_observation_status_counts']={STATUS[k]:sum(r['status']==k for r in result['native_gamma_metadata'])for k in range(7)}
 (out/'native-gamma-compare.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 text='R8m 原厂 BayerStill Gamma 曲线跟踪\n\n源片曲线解码：'+str(tone_ok)+'\n原厂关联：'+base['pairing_status']+'（时序推断）\n原厂有效Gamma元数据：'+result['native_gamma_status']+'\n原厂活动参数快照数：'+str(len(result['native_active_blocks']))+'\n模块边界数：'+str(len(result['module_boundaries']))+'\n\n'
 if result.get('native_vs_module'):
  for d in result['native_vs_module']:text+=d['stage']+'：两组432字节Gamma配置不同的半字数 '+str([b['different_u16']for b in d['bank_differences']])+'\n'
 text+='\nA040/A042是11点文件元数据；原厂Gamma对象缓存19点，并经两次曲线转换及打包后提交。当前记录用于寻找两者关系，不把节点数量或数值范围当作映射证明。稳定DDR快照、附近的对象缓存匹配和单张照片关联均不证明原厂调用时刻或ISP实际消费。模块没有新增任何曲线写入；未采到或读取中途变化的数据不能作为零曲线。\n'
 (out/'原厂Gamma跟踪结果.txt').write_text(text,encoding='utf8');return result
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('card',type=Path);p.add_argument('job',type=Path);p.add_argument('output',type=Path);a=p.parse_args();print(run(a.card,a.job,a.output)['native_gamma_status'])

"""R8l bounded pixel comparison. Never compare OTF/missing planes as zeros.
Usage: python compare_pipeline_samples.py CARD JOB OUTPUT
Native source association uses the existing single-photo timeline inference.
"""
from pathlib import Path
import argparse,hashlib,json,struct
from compare_selected_photo import compare,frames,media_path,fnv
PS_BYTES=13136
def decode_sample(b):
 if len(b)!=PS_BYTES:raise ValueError('sample extent')
 h=struct.unpack_from('<16I',b);assert h[:3]==(0x3153504e,1,PS_BYTES) and struct.unpack_from('<I',b,PS_BYTES-4)[0]==fnv(b[:-4])
 xy=list(struct.iter_unpack('<II',b[132:332]));assert h[6:8]==(6480,4320)
 expected=[(((6480*(2*x+1)//10-8)&~1),4320*(2*y+1)//10-8)for y in range(5)for x in range(5)];assert xy==expected
 return dict(status=h[3],boundary=h[4],nonce=h[5],width=h[6],height=h[7],stride=h[8],uv_offset=h[9],source_y=h[10],source_uv=h[11],begin_ns=h[12]*10**9+h[13],end_ns=h[14]*10**9+h[15],source_sha256=b[64:128].decode('ascii'),xy=xy,tiles=b[332:-4],sha256=hashlib.sha256(b).hexdigest())
def differences(a,b):
 assert a['status']==b['status']==1 and a['boundary']==b['boundary'] and a['xy']==b['xy']
 channels={k:[]for k in ['Y','Cb','Cr']}
 for k in range(25):
  at=k*512
  for ch,start,step,n in [('Y',0,1,256),('Cb',256,2,128),('Cr',257,2,128)]:
   channels[ch]+=[int(b['tiles'][at+start+i*step])-int(a['tiles'][at+start+i*step])for i in range(n)]
 return {ch:dict(bytes=len(v),mean_delta=sum(v)/len(v),mean_absolute_delta=sum(map(abs,v))/len(v),max_absolute_delta=max(map(abs,v)),changed=sum(x!=0 for x in v))for ch,v in channels.items()}
def run(card,job,out):
 out=Path(out);out.mkdir(parents=True,exist_ok=True);base=compare(card,job,out/'configuration');source=base['source'];front=next(Path(job).glob('front_*/front/selected-source.json')).parent
 main=json.loads(next(Path(job).glob('jpeg/attempt_*/result.json')).read_text('utf8'));samples={}
 for name,boundary in [('bayer-out',1),('ldc-out',2),('lut-out',3)]:
  p=front/(name+'-samples.bin')
  if p.is_file():
   s=decode_sample(p.read_bytes());assert s['status']==1 and s['boundary']==boundary and s['source_sha256']==source['source_sha256'] and s['nonce']==main['nonce'];samples[name]=s
 result=dict(schema=1,release='R8m',job=Path(job).name,source_sha256=source['source_sha256'],native_pairing=base['pairing_status'],native_pairing_is_timeline_inference=True,hardware_pixel_correctness_verified=False,module_samples={n:{k:v for k,v in s.items()if k!='tiles'}for n,s in samples.items()},native_records=[])
 if 'ldc-out'in samples and'lut-out'in samples:
  # Same spatial samples at adjacent boundaries: useful even when native OTF
  # prevents an original Bayer comparison. No claim that LUT was bypassed.
  a=dict(samples['ldc-out']);b=dict(samples['lut-out']);b['boundary']=a['boundary'];result['module_LDC_to_LUT_delta']=differences(a,b)
 if base.get('photo_event'):
  link=json.loads((front/'native-recorder-link.json').read_text('utf8'));rec=media_path(Path(card),link['recorder_report']);end=base['photo_event']['mono_ns'];begin=max(0,end-8*10**9)
  for h,p in frames(rec/'contexts.bin'):
   if h['kind']!=13 or not begin<=h['begin_ns']<=end or len(p)!=256+PS_BYTES:continue
   s=decode_sample(p[256:]);meta=struct.unpack_from('<64I',p);row=dict(**h,status=s['status'],status_text={1:'QUALIFIED_RAM_LAYOUT',2:'OTF_NO_RAM_OUTPUT',3:'NOT_RETIRED',4:'UNSUPPORTED_LAYOUT',5:'CHANGED_DURING_READ'}.get(s['status'],'UNKNOWN'),still_state=meta[2],ep_state=meta[3],sample_sha256=s['sha256']);result['native_records'].append(row)
   if s['status']==1 and base['pairing_status']=='SINGLE_PHOTO_TIMELINE_CANDIDATE' and'bayer-out'in samples:
    row['native_vs_module_Bayer_out']=differences(s,samples['bayer-out']);row['association_is_timeline_inference']=True
 statuses={x['status']for x in result['native_records']};result['native_pixel_comparison_available']=any('native_vs_module_Bayer_out'in x for x in result['native_records'])
 result['native_pixel_status']='COMPARABLE_TIMELINE_CANDIDATE'if result['native_pixel_comparison_available']else'OTF_NO_RAM_OUTPUT'if 2 in statuses else'NO_QUALIFIED_NATIVE_SAMPLE'
 (out/'pixel-compare.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 (out/'像素取样对照.txt').write_text('R8m LUT前固定区域取样\n\n原厂边界：'+result['native_pixel_status']+'\n模块取样位置：'+', '.join(samples)+'\n原厂拍摄关联：'+base['pairing_status']+'（时序推断）\n\nOTF表示直接在硬件之间传输，当前没有可读内存平面；这时不能判断原厂与模块在Bayer输出处是否一致。模块样本为25个16×16区域，并非整帧或色彩正确性证明。\n',encoding='utf8');return result
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('card',type=Path);p.add_argument('job',type=Path);p.add_argument('output',type=Path);a=p.parse_args();print(run(a.card,a.job,a.output)['native_pixel_status'])

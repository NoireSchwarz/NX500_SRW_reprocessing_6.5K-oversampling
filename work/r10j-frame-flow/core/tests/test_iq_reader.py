"""Exercise the production C reader, malformed/endian inputs, and file immutability."""
import subprocess,json,hashlib,tempfile,struct,sys,importlib.util
from pathlib import Path
R=Path(__file__).resolve().parents[1]
script=R/'upstream/reference_srw_iq_reader.py';spec=importlib.util.spec_from_file_location('ref',script);ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
exe=R/'build/test_srw_iq'
for p in map(Path,sys.argv[1:]):
 before=hashlib.sha256(p.read_bytes()).hexdigest();actual=json.loads(subprocess.check_output([exe,p]));expected=ref.read_info(p)
 assert actual['iso']==expected['iso']
 wb=expected['wb']['uncorrected_rggb'];assert actual['wb_q8']==[x*256//wb[1] for x in wb]
 assert actual['matrix']==sum([expected['decoded_tags'][n] for n in ['ColorMatrix','ColorMatrixSRGB','ColorMatrixAdobeRGB']],[])
 assert actual['black']==expected['wb']['black_rggb']
 assert hashlib.sha256(p.read_bytes()).hexdigest()==before
 print('PASS production C metadata against independent Python',p.name)
# Small independently constructed TIFFs test byte order and MakerNote-relative pointers.
for order in ['<','>']:
 def u16(x):return struct.pack(order+'H',x)
 def u32(x):return struct.pack(order+'I',x)
 b=bytearray(2048);b[:8]=(b'II' if order=='<' else b'MM')+u16(42)+u32(8)
 def ifd(at,items):
  b[at:at+2]=u16(len(items))
  for i,(tag,typ,n,v) in enumerate(items):b[at+2+12*i:at+14+12*i]=struct.pack(order+'HHII',tag,typ,n,v)
  b[at+2+12*len(items):at+6+12*len(items)]=bytes(4)
 ifd(8,[(0x10f,2,8,100),(0x110,2,6,112),(0x8769,4,1,160)])
 b[100:108]=b'SAMSUNG\0';b[112:118]=b'NX500\0';ifd(160,[(0x927c,7,1024,256)])
 key=[305,72,737,456,282,307,519,724,13,505,193];values=[(0xa020,4,key),(0xa014,4,[160]),(0xa021,4,[5824+key[0],4096+key[1],4096+key[2],9808+key[3]]),(0xa028,9,[128+x for x in key[:4]])]+[(t,9,[(x-key[i])&0xffffffff for i,x in enumerate([436,-120,-60,-44,314,-14,2,-96,350])]) for t in [0xa030,0xa031,0xa032]]
 at=512;entries=[]
 for tag,typ,v in values:
  entries.append((tag,typ,len(v),v[0] if len(v)==1 else at-256))
  if len(v)>1:
   for x in v:b[at:at+4]=u32(x);at+=4
 ifd(256,entries)
 with tempfile.TemporaryDirectory(dir=R/'build') as tmp:
  p=Path(tmp)/'synthetic.SRW';p.write_bytes(b);a=json.loads(subprocess.check_output([exe,p]));assert a['wb_q8']==[364,256,256,613]
  for name,mutate in [('truncated',lambda x:x[:270]),('note_pointer',lambda x:x.__setitem__(slice(170,174),u32(0xfffffffc)) or x),('ifd_count',lambda x:x.__setitem__(slice(256,258),u16(513)) or x),('duplicate',lambda x:x.__setitem__(slice(270,272),u16(0xa020)) or x),('zero_green',lambda x:x.__setitem__(slice(560,564),u32(key[1])) or x)]:
   bad=mutate(bytearray(b));p.write_bytes(bad);q=subprocess.run([exe,p],capture_output=True);assert q.returncode==2,(order,name,q.stdout)
 print('PASS',order,'valid + five invalid TIFF cases')
print('PASS all C reader inputs remained unchanged')

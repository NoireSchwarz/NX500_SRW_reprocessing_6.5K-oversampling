"""Read public Panasonic LUMIX Lab catalogue; downloads are data only."""
from context import *
import urllib.request,time,zipfile,io
V=W/'Panasonic-originals';V.mkdir(exist_ok=True)
BASE='https://lumix-base.jpn.panasonic.com/lutapi/v2/'
def post(endpoint,payload):
 time.sleep(.85)
 req=urllib.request.Request(BASE+endpoint,data=json.dumps(payload).encode(),headers={'Content-Type':'application/json','Accept':'application/json, application/zip','lutapp-api-key':'111','User-Agent':'NX500-PC-LUT-Import/1.0'},method='POST')
 with urllib.request.urlopen(req,timeout=40)as r:
  assert r.status==200;return r.read()
if not(V/'catalogue.json').exists():
 b=post('community/luts/filter',dict(language='en',limit='500',offset='0',order=dict(key='created',order='desc')));j=json.loads(b);put(V/'catalogue.json',j)
j=json.loads((V/'catalogue.json').read_text('utf8'))
print('Response keys:',list(j),flush=True)
rows=j.get('filter',[])
hits=[r for r in rows if any(w in json.dumps(r,ensure_ascii=False).lower()for w in ['backlight','smoky','blockbuster'])]
put(V/'matching-catalogue.json',hits)
for r in hits:print(json.dumps(r,ensure_ascii=False),flush=True)
if '--details'in sys.argv:
 for r in hits:
  p=V/(r['code']+'.json')
  if not p.exists():put(p,json.loads(post('community/lut',dict(code=r['code'],language='en'))))
  print('DETAIL',p.read_text('utf8'),flush=True)
if '--download'in sys.argv:
 saved=[]
 for r in hits:
  meta=json.loads((V/(r['code']+'.json')).read_text('utf8'));target=V/meta['file']['grid33']
  if not target.exists():target.write_bytes(post('community/luts/download',dict(code=r['code'],key='grid33')))
  with zipfile.ZipFile(target)as z:
   assert z.testzip()is None
   cubes=[x for x in z.infolist()if x.filename.lower().endswith('.cube')and not x.filename.startswith('__MACOSX/')and not Path(x.filename).name.startswith('._')]
   assert len(cubes)==1
   raw=z.read(cubes[0]);cube=V/(r['code']+'.cube');cube.write_bytes(raw)
   saved.append(dict(code=r['code'],title=meta['title'],photo_style=meta['photo_style'],endpoint=BASE+'community/luts/download',request=dict(code=r['code'],key='grid33'),zip=target.name,zip_sha256=sha(target),cube=cube.name,cube_sha256=sha(cube),archive_member=cubes[0].filename,metadata_sha256=sha(V/(r['code']+'.json'))))
   print('DOWNLOADED',meta['title'],meta['photo_style'],len(raw),flush=True)
 put(V/'download-provenance.json',saved)

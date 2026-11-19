"""Extract local original-firmware evidence; does not modify camera code."""
from context import *
sys.path.insert(0,str(PY))
from dissect.extfs import ExtFS
from elftools.elf.elffile import ELFFile
import io,re
V=W/'video-reload-research';V.mkdir(exist_ok=True)
paths=['/usr/lib/libuser-spec.so','/usr/lib/libmmutil_movie.so.0.0.0','/usr/apps/com.samsung.di-camera-app/bin/di-camera-app']
records=[]
with (R/'work/firmware/platform.img').open('rb')as f:
 fs=ExtFS(f)
 for path in paths:
  data=fs.get(path).open().read();dest=V/Path(path).name;dest.write_bytes(data)
  elf=ELFFile(io.BytesIO(data));symbols=[]
  for sec in elf.iter_sections():
   if sec.header.sh_type not in ['SHT_SYMTAB','SHT_DYNSYM']:continue
   for sym in sec.iter_symbols():
    if re.search(r'(movie|video|Movie|Video|MOVIE|VIDEO|UserSpec|userspec|user_spec)',sym.name):
     symbols.append(dict(name=sym.name,address=hex(sym.entry.st_value),size=sym.entry.st_size))
  strings=[m.group().decode('ascii')for m in re.finditer(rb'[\x20-\x7e]{8,}',data)if re.search(rb'(movie|video|Movie|Video|MOVIE|VIDEO)',m.group())]
  row=dict(path=path,sha256=sha(dest),size=len(data),symbols=symbols,strings=strings);records.append(row)
  (V/(dest.name+'.txt')).write_text('\n'.join(x['address']+' '+x['name']for x in symbols)+'\nSTRINGS\n'+'\n'.join(strings),'utf8')
put(V/'firmware-evidence.json',records)
print('Extracted original firmware evidence:',[(x['path'],len(x['symbols']),len(x['strings']))for x in records],flush=True)

"""Byte-level audit of camera-consumed text; never universal-newline decoding."""
from pathlib import Path
import hashlib,re
M='nx-lut-demo/nx-lut-lite/'
def audit_bytes(name,data):
 if not data or not data.endswith(b'\n'):raise ValueError(name+': missing final LF')
 if b'\r'in data:raise ValueError(name+': CR byte in camera text')
 if b'\0'in data or data.startswith(b'\xef\xbb\xbf'):raise ValueError(name+': NUL or BOM')
 return dict(file=name,bytes=len(data),CR_bytes=0,LF_bytes=data.count(b'\n'))
def audit_package(root,include_sums=True):
 root=Path(root);controls=['nx-lut-demo/module.manifest','nx-lut-demo/ownership/index.sha256']
 controls += [p.relative_to(root).as_posix()for p in(root/'nx-lut-demo/ownership').glob('*.manifest')]
 if include_sums:controls+=['SHA256SUMS']
 controls+=['install.sh','nx-lut-demo/deploy.sh']+[p.relative_to(root).as_posix()for p in(root/M).rglob('*.sh')]
 rows=[audit_bytes(name,(root/name).read_bytes())for name in controls]
 entries=[]
 for record in(root/'nx-lut-demo/module.manifest').read_bytes().splitlines(keepends=True):
  match=re.fullmatch(rb'([0-9a-f]{64}) ([A-Za-z0-9_./-]+)\n',record)
  if not match:raise ValueError('noncanonical raw manifest record: '+repr(record[:100]))
  digest,name=match[1].decode('ascii'),match[2].decode('ascii')
  if name.startswith('/')or'..'in name or any(part in['','.']for part in name.split('/')):raise ValueError('unsafe module path: '+name)
  path=root/M/name
  if path.is_symlink()or not path.is_file()or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise ValueError('module digest mismatch: '+name)
  entries.append(name)
 if not 20<=len(entries)<=256 or len(entries)!=len(set(entries)):raise ValueError('module entry count / duplicate')
 if set(entries)!={p.relative_to(root/M).as_posix()for p in(root/M).rglob('*')if p.is_file()}:raise ValueError('module manifest inventory mismatch')
 return dict(passed=True,module_files=len(entries),camera_text_files=len(rows),checks=rows,raw_byte_checks=True)

"""Disposable Windows/Git-sh fixture endpoints, never deployed to the camera."""
from pathlib import Path
import hashlib,os,sys
fixture=Path(os.environ['NX_TEST_FIXTURE'])
op=sys.argv[1];args=sys.argv[2:]
if op=='session':
 sys.path.insert(0,os.environ['NX_TEST_DEPS'])
 from session_file_model import SessionFileModel
 sd=(fixture/'mnt/mmc').as_posix()
 args=[a.replace('\\','/').replace(sd,'/mnt/mmc') for a in args]
 m=SessionFileModel(os.environ['NX_TEST_ARM'],args,fixture,busy=os.environ.get('NX_TEST_BUSY')=='1')
 result=m.run();print(result['stdout'],end='');sys.exit(result['exit'])
if op=='holdcheck':
 print('MODELED_LEGACY_HOLDCHECK operation='+args[0]+' locks_changed=0')
 sys.exit(0 if args==['--inventory'] else 1)
if op=='fileops':
 command=args.pop(0)
 if command=='sha256':print(hashlib.sha256(Path(args[0]).read_bytes()).hexdigest())
 elif command=='same':sys.exit(0 if Path(args[0]).read_bytes()==Path(args[1]).read_bytes() else 1)
 elif command=='kind':
  b=Path(args[0]).read_bytes();print('elf' if b[:7]==b'\x7fELF\1\1\1' and b[18:20]==b'\x28\0' else 'script' if b.startswith(b'#!/bin/sh\n') else 'unknown')
 elif command=='version':sys.exit(0 if Path(args[0]).read_bytes()==b'1.12\nNX500\n' else 1)
 elif command=='volatile-fs':pass # Only this disposable host path models tmpfs.
 elif command=='mount-info':
  root=fixture.as_posix();print(('/'+root[0].lower()+root[2:] if root[1:3]==':/' else root)+' rw')
 elif command=='unlock':Path(args[0]).rmdir()
 else:raise AssertionError(('unmodeled file utility operation',command,args))
 sys.exit(0)
if op=='mv':
 if os.environ.get('NX_TEST_FAIL_PROMOTE')=='1' and '/.nx-lut-demo-stage.' in args[0].replace('\\','/') and args[-1].replace('\\','/').endswith('/nx-lut-lite'):
  sys.exit(32)
 src=Path(args[-2]);dst=Path(args[-1]);os.replace(src,dst);sys.exit(0)
raise AssertionError((op,args))

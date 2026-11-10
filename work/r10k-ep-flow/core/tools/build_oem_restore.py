#!/usr/bin/env python3
"""Build only the independent OEM restore helper; pixel binaries stay unchanged."""
from pathlib import Path
import argparse,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--zig',type=Path,required=True);p.add_argument('--cross-runtime',type=Path,required=True);a=p.parse_args();r=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(r/'tools/build_front.py'),'--zig',str(a.zig),'--cross-runtime',str(a.cross_runtime),'--source',str(r/'src/nxl-oem-restore.c'),'--output',str(r/'bin/nxl-oem-restore.arm')],check=True)

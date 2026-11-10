#!/usr/bin/env python3
"""Build the static ARM NV16/native-JPEG adapter; no JPEG or LUT software library."""
from pathlib import Path
import argparse,subprocess,sys
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--zig',type=Path,required=True)
p.add_argument('--cross-runtime',type=Path,required=True)
a=p.parse_args()
(r.parent/'core/results').mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(r.parent/'core/tools/build_front.py'),'--zig',str(a.zig),'--cross-runtime',str(a.cross_runtime),'--source',str(r/'src/hw-post.c'),'--output',str(r/'bin/nxl-demo-post.arm')],check=True)

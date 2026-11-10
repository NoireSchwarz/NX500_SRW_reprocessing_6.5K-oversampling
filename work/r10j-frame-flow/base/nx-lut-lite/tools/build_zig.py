#!/usr/bin/env python3
"""Reproducible cross-build without a camera rootfs or bundled runtime libs.

Requires Zig 0.13.0, Python 3 and GNU nm/readelf. Link-only shared stubs describe
legacy imports; they are NOT shipped. Camera libc.so.6/libX11.so.6 supply them.
Using zig clang + zig ld.lld directly also works in restricted /proc sandboxes.
"""
import argparse
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument("--zig", required=True, type=pathlib.Path)
p.add_argument("--menu-only", action="store_true")
p.add_argument("--elftools-path", type=pathlib.Path, help="Use pyelftools instead of GNU nm/readelf")
a = p.parse_args()
zig = a.zig.resolve()
lib = zig.parent / "lib"
build = ROOT / "build"
build.mkdir(exist_ok=True)
os.environ["TMPDIR"] = str(build)
(ROOT / "bin").mkdir(exist_ok=True)

def run(*cmd):
    subprocess.run([str(x) for x in cmd], check=True)

incs = [lib / "include", ROOT / "vendor"] + [
    lib / "libc/include" / x for x in
    ("arm-linux-gnueabi", "generic-glibc", "arm-linux-any", "any-linux-any")]
base = [zig, "clang", "-target", "arm-linux-gnueabi", "-mcpu=cortex-a9",
        "-mfloat-abi=soft", "-marm", "-O0", "-fno-builtin",
        "-fno-stack-protector", "-fno-pic", "-fno-pie", "-fomit-frame-pointer",
        "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
        "-D_TIME_BITS=32", "-D_FILE_OFFSET_BITS=32", "-Wall", "-Wextra"]
for inc in incs:
    base += ["-I", inc]

def obj(name):
    src = ROOT / "src" / name
    dst = build / (name + ".o")
    run(*base, "-c", src, "-o", dst)
    return dst

div = obj("uidiv.c")
for name in (() if a.menu_only else ("cube2lut", "nx_slotwrite", "nx_regtabwrite", "nxl-lutobserve", "nxl-tablecheck")):
    run(zig, "ld.lld", "-m", "armelf_linux_eabi", "-static", "-e", "_start",
        "--strip-all", "-z", "noexecstack", "-o", ROOT / "bin" / (name + ".arm"),
        obj(name + ".c"), div)

crt = obj("crt0.S")
keys, menu = (None if a.menu_only else obj("keys.c")), obj("menu.c")
if a.elftools_path:
    sys.path.insert(0,str(a.elftools_path.resolve()))
    from elftools.elf.elffile import ELFFile
    symbols=set()
    for path in ([keys] if keys else [])+[menu,crt]:
        with path.open('rb') as f:
            elf=ELFFile(f)
            symbols.update(s.name for s in elf.get_section_by_name('.symtab').iter_symbols()
                           if s['st_shndx']=='SHN_UNDEF' and s['st_info']['bind']=='STB_GLOBAL' and s.name)
else:
    nm = subprocess.check_output(["nm", "-u", *([str(keys)] if keys else []), str(menu), str(crt)], text=True)
    symbols = set(re.findall(r"\bU\s+(\w+)", nm))
symbols -= {"main", "__aeabi_uidiv", "__aeabi_idiv", "__aeabi_idivmod"}
x11 = {s for s in symbols if s.startswith("X")}
libc = symbols - x11

def stub(name, symbols, soname):
    src = build / (name + ".c")
    src.write_text("\n".join("void *stderr;" if s == "stderr" else
                              "void %s(void) {}" % s for s in sorted(symbols)))
    out = build / (name + ".o")
    run(*base, "-fPIC", "-c", src, "-o", out)
    dst = build / soname
    run(zig, "ld.lld", "-shared", "-soname", soname, "-o", dst, out)
    return dst

cstub = stub("libc_imports", libc, "libc.so.6")
xstub = stub("x11_imports", x11, "libX11.so.6")
for name, ob, libraries in (([] if a.menu_only else [("nxl-keys", keys, [cstub])])+[("nxl-menu", menu, [cstub, xstub])]):
    run(zig, "ld.lld", "-m", "armelf_linux_eabi", "-e", "_start", "--strip-all",
        "--dynamic-linker", "/lib/ld-linux.so.3", "-z", "noexecstack", "-o",
        ROOT / "bin" / name, crt, ob, div, *libraries)
    if a.elftools_path:
        with (ROOT/"bin"/name).open('rb') as f:
            elf=ELFFile(f)
            needed=[t.needed for t in elf.get_section_by_name('.dynamic').iter_tags() if t.entry.d_tag=='DT_NEEDED']
            assert needed==[p.name for p in libraries],needed
            print(name,"NEEDED",needed)
    else:
        run("readelf", "-d", ROOT / "bin" / name)
print("Built ARM EABI soft-float binaries. Do not copy build/ to the camera.")

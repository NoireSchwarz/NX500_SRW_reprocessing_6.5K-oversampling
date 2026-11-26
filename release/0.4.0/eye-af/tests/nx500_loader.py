"""Run the actual Eye AF preload on the NX500 1.12 ARM dynamic loader."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile

from elftools.elf.elffile import ELFFile


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
WORK = ROOT / "nx500-040-eye-cnn-work"
BUILD = WORK / "yunet-resume-repair"
CACHE = WORK / "mnp-native-qemu"
SPEED = WORK / "mnp-accuracy-investigation/eye-speed"
sys.path.insert(0, str(SPEED))
sys.path.insert(0, str(ROOT / "nx500-040-release/snapshot-graph/native-test"))
from prepare_speed import TOOLING, ZIG, sha
from replay import FIRMWARE, FirmwareImage, KERNEL_HASH, Ramdisk
from importlib.util import spec_from_file_location, module_from_spec


def slots(path):
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        symbol = elf.get_section_by_name(".dynsym")
        found = [(r["r_offset"], r["r_info_type"]) for s in (".rel.plt", ".rel.dyn")
                 if (sec := elf.get_section_by_name(s)) is not None
                 for r in sec.iter_relocations() if symbol.get_symbol(r["r_info_sym"]).name == "dlsym"]
        (_, kind), = found
        rel, plt = (elf.get_section_by_name(n) for n in (".rel.dyn", ".rel.plt"))
        gap = plt["sh_addr"] - rel["sh_addr"] - rel["sh_size"]
        return kind, gap


def run(candidates):
    BUILD.mkdir(exist_ok=True)
    tool = spec_from_file_location("known_guest_init", SPEED / "validate-speed.py")
    previous = module_from_spec(tool)
    tool.loader.exec_module(previous)
    source = HERE / "nx500_loader.c"
    env = {**os.environ, "ZIG_GLOBAL_CACHE_DIR": str(WORK / "zig-global"),
           "ZIG_LOCAL_CACHE_DIR": str(BUILD / "probe-cache"), "ZIG_LIB_DIR": str(TOOLING / "lib")}
    compiler = [str(ZIG), "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9", "-O2", "-Wall", "-Wextra", "-Werror"]
    link_flags = ["-Wl,-z,lazy,-T," + str(HERE.parent / "src/nx500-arm-reloc.ld")]
    provider = BUILD / "libnxl-face-fixture.so"
    tester = BUILD / "link-probe.arm"
    subprocess.run([*compiler, "-shared", "-fPIC", "-DTEST_FACE_PROVIDER", str(source),
                    "-lc", *link_flags, "-Wl,-soname,libnxl-face-fixture.so", "-o", str(provider)], env=env, check=True)
    subprocess.run([*compiler, "-fno-pie", "-no-pie", str(source), str(provider), "-ldl",
                    "-lc", *link_flags, "-o", str(tester)], env=env, check=True)
    init_src, init = BUILD / "loader-init.c", BUILD / "loader-init.arm"
    init_src.write_text(previous.INIT, "ascii")
    subprocess.run([str(ZIG), "cc", "-target", "arm-linux-musleabi", "-mcpu=cortex_a9", "-static", "-O2",
                    "-o", str(init), str(init_src)], env=env, check=True)

    d = Ramdisk()
    d.add("/dev", stat.S_IFDIR | 0o755)
    d.add("/tmp", stat.S_IFDIR | 0o1777)
    executable = stat.S_IFREG | 0o755
    d.add("/init", executable, init.read_bytes())
    d.add("/probe", executable, tester.read_bytes())
    d.add("/usr/lib/" + provider.name, executable, provider.read_bytes())
    with (FIRMWARE / "platform.img").open("rb") as f:
        image = FirmwareImage(f)
        for n in ("/bin/sh", "/lib/ld-linux.so.3", "/lib/libdl.so.2", "/lib/libpthread.so.0", "/lib/librt.so.1"):
            d.elf(image, n)
    script = ["#!/bin/sh", "unset LD_PRELOAD LD_BIND_NOW", "/probe", "echo ARM_LINK_BASELINE=$?"]
    for i, candidate in enumerate(candidates):
        filename = f"/variant{i}.so"
        d.add(filename, executable, candidate.read_bytes())
        script += [f"echo ARM_LINK_VARIANT_{i}", f"LD_DEBUG=bindings LD_PRELOAD={filename} /probe",
                   f"echo ARM_LINK_EXIT_{i}=$?"]
    script.append("exit 0")
    d.add("/verify", executable, ("\n".join(script) + "\n").encode("ascii"))
    initrd = BUILD / "loader-initramfs.gz"
    d.pack(initrd)
    kernel = CACHE / "vmlinuz"
    assert sha(kernel) == KERNEL_HASH
    with tempfile.TemporaryDirectory(prefix="nx500-link-") as staging:
        tmp = Path(staging)
        shutil.copyfile(kernel, tmp / "vmlinuz")
        shutil.copyfile(initrd, tmp / "loader-initramfs.gz")
        command = [str(CACHE / "qemu-bin/qemu-system-arm.exe"), "-machine", "virt", "-cpu", "cortex-a15",
                   "-m", "256M", "-nodefaults", "-nic", "none", "-monitor", "none", "-display", "none",
                   "-serial", "stdio", "-no-reboot", "-kernel", "vmlinuz", "-initrd", "loader-initramfs.gz",
                   "-append", "console=ttyAMA0 rdinit=/init panic=2 loglevel=3"]
        guest = subprocess.run(command, cwd=tmp, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=55)
    log = BUILD / "loader-console.log"
    log.write_bytes(guest.stdout)
    text = guest.stdout.decode("utf-8", "replace")
    if guest.returncode or "NXL_GUEST_CHECK 0" not in text or "ARM_LINK_BASELINE=0" not in text:
        raise RuntimeError(text[-2800:])
    ret = dict(re.findall(r"ARM_LINK_EXIT_(\d+)=([0-9]+)", text))
    checks = []
    for i, path in enumerate(candidates):
        result, gap = slots(path)
        callback = "_ZN20CFaceDetection_Linux13FaceDetectionEPhiiii"
        bound = any(f"binding file /probe [0] to /variant{i}.so [0]" in line and callback in line
                    for line in text.splitlines())
        checks.append({"name": path.name, "sha256": sha(path), "dlsym_relocation": result,
                       "relocation_gap_bytes": gap, "wrapper_bound": bound, "return": int(ret[str(i)])})
    (BUILD / "loader-validation.json").write_text(json.dumps(checks, indent=2) + "\n", encoding="utf-8")
    print("\n".join(line for line in text.splitlines()
                    if "ARM_LINK_" in line or "arm_loader " in line or "Segmentation fault" in line))
    print(json.dumps(checks, indent=2))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("candidate", type=Path, nargs="+")
    run(p.parse_args().candidate)

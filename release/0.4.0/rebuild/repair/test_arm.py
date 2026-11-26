"""Replay both capture guards with the original ARM service manager in RAM."""

import argparse
import io
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tarfile

from build import CANON, PROFILE, RUNTIME, UNITS, VARIANTS, digest, put, read_inputs, replace

ORIGIN = b"11111111-1111-4111-8111-111111111111\nEYE\nFAST_START_EYE_AF_ROOTLIB_100_300\n"


def profile(pkg, historical):
    root = CANON if historical else f"usr/local/lib/{pkg.name}"
    files = {f"{UNITS}/nxl-full040-resume.service": (0o644, (pkg / "nxl-full040-resume.service").read_bytes()),
             PROFILE: (0o644, (pkg / "capture-profile.conf").read_bytes()),
             f"{root}/capture-origin": (0o600, ORIGIN)}
    for name in RUNTIME:
        files[f"{root}/{name}"] = (0o755, (pkg / name).read_bytes())
    if not historical:
        for name in ("launch", "wake"):
            files[f"{CANON}/{name}.sh"] = (0o755, (pkg / f"{name}-select.sh").read_bytes())
    files[f"{CANON}/verify-before-capture.sh"] = (0o755, (pkg / "verify-before-capture.sh").read_bytes())
    # The existing fixture uses this sentinel only to build its RAM unit layout.
    # The test installs the real guarded program inside that layout below.
    files["usr/bin/hibernation.sh"] = (0o755, b"#!/bin/sh\n/bin/sh /usr/local/lib/nxl-full040/verify-before-capture.sh --at-image-write || exit 1\n"
                                      b"/bin/dd if=/dev/zero of=$SNAPSHOT_DEV bs=512 count=1\n")
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w") as archive:
        for name, (mode, data) in files.items():
            entry = tarfile.TarInfo(name)
            entry.mode, entry.size = mode, len(data)
            archive.addfile(entry, io.BytesIO(data))
        entry = tarfile.TarInfo(f"{UNITS}/hib_post.service.wants/nxl-full040-resume.service")
        entry.type, entry.linkname = tarfile.SYMTYPE, "/etc/systemd/system/nxl-full040-resume.service"
        archive.addfile(entry)
    return output.getvalue()


def ram_writer(pkg, late, historical):
    script = (pkg / "hibernation.sh").read_text("ascii")
    zram = "echo simulated > /run/simulated-zram"
    if late:
        library = CANON if historical else f"usr/local/lib/{pkg.name}"
        zram += f"\necho changed >> /{library}/nxl-eye-af.so"
    script = replace(script, '/bin/sh -c "echo -n $((50*1024*1024)) > /sys/block/zram0/disksize"', zram)
    for original, virtual in (("mkswap /dev/zram0", ":"), ("swapon /dev/zram0", ":"),
                              ("echo timer > /sys/class/leds/card/trigger", "echo timer > /run/simulated-led"),
                              ("/bin/dd if=/dev/zero of=$SNAPSHOT_DEV bs=512 count=1", "echo permitted > /run/simulated-header"),
                              ("mkswap -c $SNAPSHOT_DEV", ":"), ("swapon $SNAPSHOT_DEV", ":"),
                              ("\nmount -o ro,remount /\n", "\n:\n")):
        script = replace(script, original, virtual)
    if not historical:
        script = replace(script, "/sbin/swapoff /dev/zram0 || :", "echo simulated > /run/simulated-zram-off")
    return script.encode("ascii")


def build_observer(native, output):
    source = (native.HERE / "dispatch_guest.c").read_text("utf-8")
    a = source.index('        printf("NX500 CAPTURE passes=')
    b = source.index("\n    }", a)
    source = source[:a] + '''        printf("R11_CAPTURE gates=%d,%d header=%d ready=%d led=%d zram=%d cleanup=%d\\n",
               passes, stops, header, !access("/tmp/.hib_ready", F_OK), !access("/run/simulated-led", F_OK),
               !access("/run/simulated-zram", F_OK), !access("/run/simulated-zram-off", F_OK));
        fflush(stdout);
        finish(0);''' + source[b:]
    c = output / "observer.c"
    c.write_text(source, "utf-8")
    elf = output / "observer.arm"
    subprocess.run([str(native.TOOLING / "ziglang/zig.exe"), "cc", "-target", "arm-linux-musleabi", "-mcpu=cortex_a9", "-static",
                    "-Os", "-s", str(c), "-o", str(elf)], check=True, timeout=100,
                   env={**os.environ, "ZIG_GLOBAL_CACHE_DIR": str(output / "zig-global"), "ZIG_LOCAL_CACHE_DIR": str(output / "zig-local")})
    return elf.read_bytes()


def exercise(native, pkg, cache, output, observer, historical=False):
    image_profile = profile(pkg, historical)
    results = {}
    qemu = cache / "qemu-bin/qemu-system-arm.exe"
    kernel = cache / "vmlinuz"
    if digest(kernel.read_bytes()) != native.KERNEL_HASH:
        raise ValueError("PC kernel identity differs")
    cases = {"capture-combined": "gates=3,0 header=1 ready=1 led=1 zram=1 cleanup=0",
             "capture-stale-manager": "gates=0,2 header=0 ready=1 led=0 zram=0 cleanup=0",
             "capture-candidate-changed": "gates=0,2 header=0 ready=1 led=0 zram=0 cleanup=0",
             "capture-late-changed": "gates=2,1 header=0 ready=1 led=0 zram=1 cleanup=1"}
    if historical:
        cases = {"capture-late-changed": "gates=1,1 header=0 ready=0 led=1 zram=1 cleanup=0"}
    for case, expected in cases.items():
        disk = native.guest(image_profile, observer, case)
        with (native.FIRMWARE / "platform.img").open("rb") as firmware:
            disk.elf(native.FirmwareImage(firmware), "/bin/touch")
        disk.entries["usr/bin/hibernation.sh"] = (stat.S_IFREG | 0o755, ram_writer(pkg, case == "capture-late-changed", historical))
        if case == "capture-candidate-changed":
            name = f"usr/local/lib/{pkg.name}/nxl-eye-af.so"
            mode, data = disk.entries[name]
            disk.entries[name] = (mode, data + b"changed")
        path = output / f"{pkg.name}-{case}"
        image, log = path.with_suffix(".gz"), path.with_suffix(".log")
        disk.pack(image)
        cmd = [str(qemu), "-machine", "virt", "-cpu", "cortex-a15", "-m", "320M", "-nodefaults", "-nic", "none",
               "-monitor", "none", "-display", "none", "-serial", "stdio", "-no-reboot", "-kernel", str(kernel),
               "-initrd", str(image), "-append", "console=ttyAMA0 rdinit=/init panic=2 loglevel=3"]
        with log.open("wb") as stream:
            result = subprocess.run(cmd, stdout=stream, stderr=subprocess.STDOUT, timeout=40)
        trace = log.read_text(errors="replace")
        assert result.returncode == 0 and "R11_CAPTURE " + expected in trace and "NX500 DISPATCH RESULT 0" in trace, (case, trace[-2000:])
        reasons = [line.split("=", 1)[1] for line in trace.splitlines() if line.startswith("reason=")]
        if case.endswith("candidate-changed"):
            assert reasons == ["UNIFIED_PAYLOAD_MISMATCH"] * 2, reasons
        if case.endswith("late-changed"):
            assert reasons.count("UNIFIED_PAYLOAD_MISMATCH") == 1, reasons
        results[case] = {"observation": expected, "manager_reasons": reasons, "hardware_writes": 0}
        print("PASS", pkg.name, case, flush=True)
    return {"service_profile_sha256": digest(image_profile), "cases": results}


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("tree", type=Path)
    cli.add_argument("harness", type=Path)
    cli.add_argument("cache", type=Path)
    cli.add_argument("output", type=Path)
    cli.add_argument("published", type=Path)
    cli.add_argument("focus", type=Path)
    args = cli.parse_args()
    args.output.mkdir()
    sys.path.insert(0, str(args.harness.resolve()))
    import dispatch as native

    observer = build_observer(native, args.output.resolve())
    results = {name: exercise(native, args.tree / "profiles" / name, args.cache, args.output, observer) for name in VARIANTS.values()}
    _, r1, _ = read_inputs(args.published, args.focus)
    baseline = args.output / "published-r1"
    for name, blob in r1.items():
        put(baseline, name, blob)
    original_behavior = exercise(native, baseline, args.cache, args.output, observer, historical=True)
    with (native.FIRMWARE / "platform.img").open("rb") as firmware:
        original = native.FirmwareImage(firmware)
        pins = {path: digest(original.file(path)) for path in (native.MANAGER, "/usr/lib/systemd/system/hib_post.path",
                                                               "/usr/lib/systemd/system/hib_post.service")}
    (args.output / "manager.json").write_text(json.dumps({"original_ARM": pins, "profiles": results, "published_r1_baseline": original_behavior,
        "scope": "Samsung ARM systemd as PID 1. zram, LED and snapshot writes are represented by RAM markers."}, indent=2) + "\n", "utf-8")


if __name__ == "__main__":
    main()

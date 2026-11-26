"""Build the ARM shared object or run its isolated face-detection replay."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--zig", default="zig", help="Zig 0.13.0 executable")
    cli.add_argument("--out", type=Path, default=HERE / "out", help="build output directory")
    cli.add_argument("--test", action="store_true", help="also run the Cortex-A9 fixtures")
    cli.add_argument("--yunet", action="store_true", help="build the 32x32 INT8 YuNet backend")
    args = cli.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cc = [args.zig, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9"]
    sources = ["src/eye-af.c"]
    name = "nxl-eye-af.so"
    backend = []
    link = []
    if args.yunet:
        subprocess.run([sys.executable, "-B", "model/pack_yunet.py", "model/yunet-2026may.onnx",
                        "model/yunet-calibration.json", str(out / "yunet_parameters.h")], cwd=HERE, check=True)
        sources.append("src/eye_yunet.c")
        backend = ["-DNXL_EYE_YUNET", "-I", str(out)]
        link = ["-Wl,-T,src/nx500-arm-reloc.ld"]
        name = "nxl-eye-af-yunet.so"
    subprocess.run([*cc, "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror",
                    *backend, "-o", str(out / name), *sources, "-lc", "-ldl", "-lrt",
                    *link,
                    "-Wl,-soname,nxl-eye-af.so,-z,relro,-z,now,--strip-all",
                    "-Wl,--version-script=src/exports.map"], cwd=HERE, check=True)
    if args.test:
        fixture = out / "eye-replay.elf"
        obj = out / "eye-replay.o"
        subprocess.run([*cc, "-O2", "-fno-builtin", "-fno-stack-protector", "-fno-pie",
                        "-lc", "-c", "-o", str(obj), "tests/eye_replay.c"], cwd=HERE, check=True)
        # Link the whole fixture. Its observation arrays are read by the emulator.
        # Compiling the ARM shared object above also primes Zig's runtime cache.
        info = json.loads(subprocess.check_output([args.zig, "env"], encoding="utf-8"))
        cache = Path(os.environ.get("ZIG_GLOBAL_CACHE_DIR", info["global_cache_dir"]))
        for library in sorted(cache.glob("o/*/libcompiler_rt.a")):
            raw = library.read_bytes()
            first = raw.find(b"\x7fELF")
            if first >= 0 and raw[first+5] == 1 and raw[first+18:first+20] == b"\x28\x00":
                break
        else:
            raise SystemExit("Zig has not created an ARM compiler runtime in its cache.")
        subprocess.run([args.zig, "ld.lld", "-m", "armelf_linux_eabi", "-static", "-e", "verify",
                        "-o", str(fixture), str(obj), str(library)], cwd=HERE, check=True)
        subprocess.run([sys.executable, "tests/replay.py", str(fixture)], cwd=HERE, check=True)
        if args.yunet:
            for suffix, flags in (("", []), ("-scalar", ["-U__ARM_NEON"])):
                elf = out / f"yunet-replay{suffix}.elf"
                obj = out / f"yunet-replay{suffix}.o"
                subprocess.run([*cc, *backend, *flags, "-O2", "-fno-builtin", "-fno-stack-protector",
                                "-fno-pie", "-c", "-o", str(obj), "tests/yunet_replay.c"], cwd=HERE, check=True)
                subprocess.run([args.zig, "ld.lld", "-m", "armelf_linux_eabi", "-static", "-e", "verify_points",
                                "-o", str(elf), str(obj), str(library)], cwd=HERE, check=True)
            subprocess.run([sys.executable, "tests/replay_yunet.py", str(out / "yunet-replay.elf"),
                            str(out / "yunet-replay-scalar.elf")], cwd=HERE, check=True)


if __name__ == "__main__":
    main()

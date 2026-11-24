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
    cli.add_argument("--test", action="store_true", help="also run the Cortex-A9 fixtures")
    args = cli.parse_args()
    out = HERE / "out"
    out.mkdir(exist_ok=True)
    cc = [args.zig, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9"]
    subprocess.run([*cc, "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-o", str(out / "nxl-eye-af.so"), "src/eye-af.c", "-lc", "-ldl", "-lrt",
                    "-Wl,-soname,nxl-eye-af.so,-z,relro,-z,now,--strip-all",
                    "-Wl,--version-script=src/exports.map"], cwd=HERE, check=True)
    if args.test:
        fixture = out / "eye-replay.elf"
        obj = out / "eye-replay.o"
        subprocess.run([*cc, "-O2", "-fno-builtin", "-fno-stack-protector", "-fno-pie",
                        "-lc", "-c", "-o", str(obj), "tests/eye_replay.c"], cwd=HERE, check=True)
        # Link the whole fixture. Its observation arrays are read by the emulator.
        # Compiling the ARM shared object above also primes Zig's runtime cache.
        info = json.loads(subprocess.check_output([args.zig, "env"], text=True))
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


if __name__ == "__main__":
    main()

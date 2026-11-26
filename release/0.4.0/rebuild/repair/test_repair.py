"""Replay the generated installers and selectors with disposable filesystem endpoints."""

import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from unittest.mock import patch

from build import CAPTURE_CONF, HERE, PROFILE, RUNTIME, UNITS, VARIANTS, digest, read_inputs

sys.path.insert(0, str(HERE.parent))
from test_rebuild import BOOT, CAPTURE, OPS, SH, fixtures, load, run, write

sys.modules["card_files"] = load("card_files", HERE.parent / "coexist_rebuild.py")
import stage


CTL = '''#!/bin/sh
echo "ctl $*" >> sequence
case "$*" in
    *rescue*) if [ -f rescue-fails ]; then exit 9; fi;;
    *di-camera-app.service*)
        app=/usr/apps/com.samsung.di-camera-app/bin/di-camera-app
        [ ! -f etc/systemd/system/di-camera-app.service.d/68-nxl-full040.conf ] || app='/bin/sh /usr/local/lib/nxl-full040/launch.sh'
        printf 'ExecStart={ path=%s ; argv[]=%s ; ignore_errors=no ; start_time=[n/a] }\\n' "${app%% *}" "$app";;
    *hib_post.service*) if [ -f reject-graph ]; then echo 'Wants='; else echo 'Wants=nxl-full040-resume.service'; fi;;
    *nxl-full040-resume.service*) echo 'ExecStart={ path=/bin/sh ; argv[]=/bin/sh /usr/local/lib/nxl-full040/wake.sh ; ignore_errors=no ; }';;
esac
'''


def worker(source, full, r1, case):
    with tempfile.TemporaryDirectory(prefix="nx500-r11-worker-") as path:
        root = Path(path)
        fixtures(root)
        write(root / "usr/bin/systemctl", CTL)
        write(root / "bin/sleep", "pinned fixture for the stock loader executable\n")
        pkg = root / "mnt/mmc" / source.name
        shutil.copytree(source, pkg)
        write(pkg / "request", "CHECK\n" if case == "check" else "REBUILD\n")
        for name in stage.TRIGGERS:
            write(pkg.parent / name, (pkg / "activation" / name).read_bytes())
        for name, data in full.items():
            prefix = "module/nx-lut-demo/nx-lut-lite/"
            if name.startswith(prefix):
                write(root / "opt/usr/nx-lut-lite" / name.removeprefix(prefix), data)
        write(root / "opt/usr/nx-lut-lite/state/module.manifest", full["module/nx-lut-demo/module.manifest"])
        write(root / "opt/usr/nx-lut-lite/state/demo51-installed", "install_revision=release-0.4.0-rc6.14.12-log117\n")
        prev = root / "opt/usr/nxl-full040"
        canon = root / "usr/local/lib/nxl-full040"
        r1_start = case.startswith("r1-")
        flag = root / "etc/snapshot"
        if r1_start:
            for name in (*RUNTIME, "readout", "verify-before-capture.sh"):
                write(canon / name, r1[name])
            for name in ("claim.arm", "fileops.arm"):
                write(prev / name, r1[name])
            write(prev / "armed", BOOT + "\nEYE\nFAST_START_EYE_AF_ROOTLIB_100_300\n")
            write(prev / "launch-claim", "retained-previous-claim\n")
            write(canon / "capture-origin", (prev / "armed").read_bytes())
            write(root / CAPTURE_CONF, full["nxl-full040/capture.conf"])
            write(root / PROFILE, r1["capture-profile.conf"])
            write(root / (UNITS + "/nxl-full040-resume.service"), r1["nxl-full040-resume.service"])
            write(root / (UNITS + "/hib_post.service.wants/nxl-full040-resume.service"),
                  "/etc/systemd/system/nxl-full040-resume.service\n")
            for name in ("hibernation.sh", "make_snapshot.sh"):
                write(root / "usr/bin" / name, r1[name])
        else:
            flag.unlink()

        program = (source / "worker.sh").read_text("ascii").replace("root=\n", '''root=.
sync(){ :; }
mkdir(){ if [ "$1" = -m ]; then shift 2; fi; command mkdir "$@"; }
stat(){
    if [ "$1" = -c ] && [ "$2" = %d ]; then
        if [ "$3" = ./opt/usr ]; then echo 14; else echo 10; fi
    else command stat "$@"; fi
}
ln(){ if [ "$1" = -s ]; then printf '%s\\n' "$2" > "$3"; else command ln "$@"; fi; }
readlink(){ case "$1" in */hib_post.service.wants/nxl-full040-resume.service) cat "$1";; *) command readlink "$@";; esac; }
''')
        program = program.replace('[ -L "$link" ]', '[ -f "$link" ]').replace('[ -b "$dev" ]', '[ -f "$dev" ]')
        program = program.replace("307199", "8").replace('cd / || stop FINAL_CWD', 'cd . || stop FINAL_CWD')
        program = program.replace('>/dev/console', '>"$root/dev/console"')
        for name, checksum, reason in re.findall(r'pin "\$root/([^"\n]+)" ([a-f0-9]{64}) \|\| stop (STOCK_[A-Z_]+)', program):
            if r1_start and reason == "STOCK_WRITER_CHANGED":
                continue
            path = root / name
            if not path.exists():
                write(path, "fixture firmware endpoint " + name)
            program = program.replace(checksum, digest(path.read_bytes()))
        sleep_pin = re.search(r'pin "\$stock_sleep" ([a-f0-9]{64})', program)[1]
        program = program.replace(sleep_pin, digest((root / "bin/sleep").read_bytes()))
        write(root / "usr/lib/libmmfcore.so", "core fixture\n")
        program = program.replace("b3e7cd2987282d856a8f95b8768d44ea2f4f3804684a391de0567c10dada73c8",
                                  digest((root / "usr/lib/libmmfcore.so").read_bytes()))
        claim = "#!/bin/sh\nprintf 'file=%s\\n' \"$6\" > \"$1/launch-claim\"\n"
        for name, value in {"fileops.arm": OPS, "claim.arm": claim,
                            "nxl-menu": "#!/bin/sh\nexit 0\n",
                            "loader-observe.sh": "#!/bin/sh\ntest ! -f loader-fails\n",
                            "dispatch.sh": (source / "dispatch.sh").read_text("ascii").replace("task_root=\n", "task_root=.\n")}.items():
            prod = digest((source / name).read_bytes())
            fixture = digest(value.encode())
            program = program.replace(prod, fixture)
            write(pkg / name, value)
            if r1_start and name in ("fileops.arm", "claim.arm"):
                write(prev / name, value)
                if name in RUNTIME:
                    write(canon / name, value)
        if case in ("stock-graph-reject", "r1-graph-reject"):
            write(root / "reject-graph", "1")
        if case == "old-bootstrap":
            write(prev / "armed", BOOT + "\nEYE\nobsolete\n")
        if case == "foreign-master":
            write(root / (UNITS + "/multi-user.target.wants/nx-on-wake.service"), "external\n")
        if case == "foreign-sd":
            write(pkg.parent / "nx-on-wake/on-wake", "external\n")
        if case == "corrupt-eye":
            with (pkg / "nxl-eye-af.so").open("ab") as out:
                out.write(b"corrupt")
        if case == "loader-reject":
            write(root / "loader-fails", "1")
        if case == "rescue-reject":
            write(root / "rescue-fails", "1")
        # These pins are all preflight reads. Precompute their actual fixture
        # digests once to avoid hundreds of Windows shell process launches.
        # The ARM replay executes the real SHA implementation in the write gate.
        paths = []
        for relative, dirs, files in os.walk(root):
            for name in files:
                path = Path(relative) / name
                paths.append(shlex.quote("./" + path.relative_to(root).as_posix() + ":" + digest(path.read_bytes())))
        fast_pin = 'pin() { regular "$1" || return 1; case "$1:$2" in\n' + "|\n".join(paths).replace("|\n", "|\\\n") + ') return 0;;\n*) return 1;;\nesac; }'
        program = program.replace('pin() { regular "$1" && [ "$("$OP" sha256 "$1")" = "$2" ]; }', fast_pin)
        program = program.replace('stock_sleep=$(readlink -f "$root/bin/sleep")', 'stock_sleep=$root/bin/sleep')
        critical = (flag, root / CAPTURE_CONF, root / PROFILE, root / "usr/bin/hibernation.sh", root / "usr/bin/make_snapshot.sh",
                    canon / "launch.sh", canon / "wake.sh", canon / "nxl-eye-af.so", prev / "armed", prev / "launch-claim")
        before = {p: p.read_bytes() if p.exists() else None for p in critical}
        write(root / "worker.sh", program)
        trial = run(root, "worker.sh")
        report, = (pkg.parent / "NXDEMO51").glob("FAST_START_EYE_AF_ROOTLIB_*")
        result = (report / "result.txt").read_text("ascii")
        log = (report / "prepare.log").read_text("ascii")
        expected = {"stock": "OEM_RESCUE_REQUEST", "r1-stock": "OEM_RESCUE_REQUEST", "check": "CHECKED_READY",
                    "stock-graph-reject": "RESUME_DEPENDENCY_NOT_LOADED", "r1-graph-reject": "RESUME_DEPENDENCY_NOT_LOADED",
                    "old-bootstrap": "PARTIAL_OR_FOREIGN_INSTALL", "foreign-master": "LEGACY_WAKE_ENABLED",
                    "foreign-sd": "FOREIGN_SD_BOOT_ENTRY", "corrupt-eye": "PAYLOAD_IDENTITY",
                    "loader-reject": "ARM_LIBRARY_LOAD_FAILED", "rescue-reject": "OEM_RESCUE_REQUEST"}[case]
        assert f"reason={expected}\n" in result, (case, result, log, trial.stderr)
        (sequence := root / "sequence").touch()
        handed = expected == "OEM_RESCUE_REQUEST"
        assert sequence.read_text().count("ctl rescue") == int(handed), case
        assert not any((pkg.parent / name).exists() for name in stage.TRIGGERS), case
        assert (root / "dev/mmcblk0p9").read_bytes() == b"snapshot-preserved".ljust(4096, b"#"), case
        if handed:
            assert (root / "usr/local/lib" / source.name / "nxl-eye-af.so").read_bytes() == (source / "nxl-eye-af.so").read_bytes()
            if r1_start:
                assert all(p.read_bytes() == before[p] for p in (canon / "nxl-eye-af.so", prev / "armed", prev / "launch-claim"))
            if case == "rescue-reject":
                candidate = root / "usr/local/lib" / source.name
                assert (candidate / "capture-origin.rescue-rejected").is_file() and not (candidate / "capture-origin").exists()
                assert trial.returncode == 9
            else:
                assert trial.returncode == 0
        else:
            assert all((p.read_bytes() if p.exists() else None) == b for p, b in before.items()), case
        return {"expected_reason": expected, "rescue_calls": int(handed), "previous_image_bytes_preserved": True}


def selectors(source):
    with tempfile.TemporaryDirectory(prefix="nx500-r11-select-") as path:
        root = Path(path)
        fixtures(root)
        report = "FAST_START_EYE_AF_ROOTLIB_100_300"
        rt = root / "usr/local/lib" / source.name
        candidate = root / "opt/usr" / source.name
        write(rt / "capture-origin", f"{BOOT}\nEYE\n{report}\n")
        write(root / "usr/apps/com.samsung.di-camera-app/bin/di-camera-app", "#!/bin/sh\nprintf 'stock:%s\\n' \"${HIB:-unset}\"\n")
        for name in ("launch", "wake"):
            script = (source / (name + "-select.sh")).read_text("ascii").replace("root=\n", "root=.\n")
            write(root / name, script)
            write(rt / (name + ".sh"), "echo candidate\n")

        def select(name):
            result = run(root, name)
            assert result.returncode == 0, (name, result.stderr)
            return result.stdout.decode().strip()

        with patch.dict(os.environ, {"HIB": "1"}):
            assert select("launch") == "stock:unset" and select("wake") == ""
            for name in ("launch", "wake"):
                write(rt / ("legacy-" + name + ".sh"), "echo previous\n")
            assert select("launch") == select("wake") == "previous"
            proof = root / "run/nxl-full040-capture/image-header.1.txt"
            write(proof, f"boot_id={CAPTURE}\ncapture_report={report}\neye_build={source.name}\ndecision=PASS\n")
            assert select("launch") == "previous"
            write(root / "proc/sys/kernel/random/boot_id", CAPTURE + "\n")
            assert select("launch") == "candidate" and select("wake") == "previous"
            write(candidate / "launch-claim", f"boot_id={CAPTURE}\nfile=/usr/local/lib/{source.name}/nxl-eye-af.so\n")
            assert select("wake") == "candidate"
    return {"stock_and_previous_fallback": True, "current_generation_proof_and_process_required": True}


def card(source):
    with tempfile.TemporaryDirectory(prefix="nx500-r11-card-") as path, patch.object(stage, "identity", lambda *args: None):
        sd = Path(path)
        saved = {"DCIM/family.jpg": b"fixture photo", "NXDEMO51/logs/install.log": b"fixture log", "backup.img": b"fixture backup"}
        for name, data in saved.items():
            write(sd / name, data)
        blocked = sd / "nx-on-wake/on-wake"
        write(blocked, "existing external callback")
        try:
            stage.prepare(source, sd, 1, "CHECK")
        except FileExistsError:
            assert blocked.is_file() and not (sd / source.name).exists()
        else:
            raise AssertionError("External wake callback did not block staging")
        blocked.rename(sd / "external-retained")
        assert stage.prepare(source, sd, 1, "CHECK")["request"] == "CHECK"
        for name in stage.TRIGGERS:
            (sd / name).rename(sd / f"consumed-{name}")
        assert stage.prepare(source, sd, 1, "REBUILD")["request"] == "REBUILD"
        assert (sd / source.name / "request").read_bytes() == b"REBUILD\n"
        assert all((sd / name).read_bytes() == data for name, data in saved.items())
    return {"foreign_callback_preserved": True, "check_then_rebuild": True, "media_and_logs_preserved": True}


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("tree", type=Path)
    cli.add_argument("published", type=Path)
    cli.add_argument("focus", type=Path)
    cli.add_argument("receipt", type=Path)
    args = cli.parse_args()
    full, r1, _ = read_inputs(args.published, args.focus)
    results = {}
    for name in VARIANTS.values():
        source = args.tree / "profiles" / name
        profiles = stage.verify_pkg(source)
        for file in (file for file in profiles if file.endswith(".sh")):
            subprocess.run([SH, "-n", str(source / file)], check=True, timeout=10)
        cases = ("stock", "r1-stock", "check", "stock-graph-reject", "r1-graph-reject", "old-bootstrap", "foreign-master",
                 "foreign-sd", "corrupt-eye", "loader-reject", "rescue-reject")
        results[name] = {"payload": profiles, "shell_worker": {case: worker(source, full, r1, case) for case in cases},
                         "selectors": selectors(source), "sd": card(source)}
        print("PASS", name, flush=True)
    args.receipt.write_text(json.dumps(results, indent=2) + "\n", "utf-8")


if __name__ == "__main__":
    main()

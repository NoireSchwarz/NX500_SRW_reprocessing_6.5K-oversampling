"""Execute the installed scripts against disposable PC endpoints; never access a camera."""
import argparse
from contextlib import redirect_stdout
from hashlib import sha256
from importlib.util import spec_from_file_location, module_from_spec
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from unittest.mock import patch
from zipfile import ZipFile

HERE = Path(__file__).resolve().parent
SH = "C:/Program Files/Git/bin/sh.exe"
BOOT = "11111111-1111-4111-8111-111111111111"
CAPTURE = "22222222-2222-4222-8222-222222222222"
OPS = '''#!/bin/sh
case "$1" in
sha256) case "$2" in *.img) exit 64;; esac; line=$(sha256sum "$2") && echo "${line%% *}";;
same) cmp -s "$2" "$3";;
version) grep -qx fixture "$2";;
*) exit 2;;
esac
'''


def digest(data):
    return sha256(data).hexdigest()


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode() if isinstance(data, str) else data)


def load(name, path):
    spec = spec_from_file_location(name, path)
    mod = module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run(root, path, *args):
    return subprocess.run([SH, "-c", 'PATH=/usr/bin:/bin; export PATH; /bin/sh "$@"', "fixture", path, *args], cwd=root,
                          capture_output=True, timeout=120)


def payload_folder(bundle):
    pins = json.loads((bundle / "rebuild-checksums.json").read_text("utf-8"))
    folders = {name.split("/", 1)[0] for name in pins}
    folder, = folders
    return folder


def fixtures(root):
    files = {"proc/sys/kernel/random/boot_id": BOOT + '\n', "sys/power/bootmode": "2\n",
             "sys/class/extcon/USB/state": "0\n", "proc/swaps": "Filename Type Size Used Priority\n",
             "proc/mounts": "/dev/root / ext4 ro 0 0\n/dev/mmcblk0p14 /opt/usr ext4 rw 0 0\n/dev/mmcblk1p1 /mnt/mmc exfat rw 0 0\n",
             "proc/uptime": "12.34 0.0\n", "etc/version.info": "fixture\n", "etc/snapshot": "1\n",
             "sys/class/power_supply/battery/capacity": "100\n", "sys/class/block/mmcblk0p9/size": "8\n",
             "dev/mmcblk0p9": b"snapshot-preserved".ljust(4096, b"#"), "dev/console": "",
             "bin/mount": '#!/bin/sh\necho "mount $*" >> sequence\n',
             "bin/dd": '#!/bin/sh\nexec /bin/dd "$@"\n', "bin/tar": '#!/bin/sh\nexec /bin/tar "$@"\n',
             "bin/df": '#!/bin/sh\nprintf "Filesystem 1024-blocks Used Available Capacity Mounted\\nfixture 999999 0 999999 1%% /mnt/mmc\\n"\n'}
    for name, value in files.items():
        write(root / name, value)
    for name in ("usr/local/lib", "opt/usr", "etc/systemd/system", "run", "tmp"):
        (root / name).mkdir(parents=True, exist_ok=True)


def worker_test(bundle, baseline, case, mnp_runtime=None, yunet_runtime=None, primary_runtime=None, predecessor=None):
    with tempfile.TemporaryDirectory(prefix="nx-unified-worker-") as work:
        root = Path(work)
        fixtures(root)
        pkg = root / "mnt/mmc" / payload_folder(bundle)
        supervised = pkg.name in ("nxl-full040-yunet2-svc", "nxl-full040-yunet2-pid1")
        primary = case.startswith("primary-")
        direct = pkg.name == "nxl-full040-yunet2-direct" or primary
        shutil.copytree(bundle / pkg.name, pkg)
        state, rt, etc = [root / n for n in ("opt/usr/nxl-full040", "usr/local/lib/nxl-full040", "etc/systemd/system")]
        r1 = case.startswith("r1-")
        mnp = case.startswith("mnp-")
        yunet = case.startswith("yunet1-")
        writers = ("hibernation.sh", "make_snapshot.sh")
        gate = etc / "hibernation.service.d/75-nxl-full040-prewrite.conf"
        with ZipFile(baseline) as old:
            for name in old.namelist():
                if name.startswith("module/nx-lut-demo/nx-lut-lite/") and not name.endswith('/'):
                    write(root / "opt/usr/nx-lut-lite" / name.split("nx-lut-lite/", 1)[1], old.read(name))
            write(root / "opt/usr/nx-lut-lite/state/module.manifest", old.read("module/nx-lut-demo/module.manifest"))
            for name in ("fileops.arm", "claim.arm"):
                write(state / name, old.read("nxl-full040/" + name))
            for name in ("launch.sh", "dispatch.sh", "readout", "full-menu.sh", "nxl-eye-af.so"):
                write(rt / name, old.read("nxl-full040/" + name))
            write(etc / "di-camera-app.service.d/68-nxl-full040.conf", old.read("nxl-full040/capture.conf"))
            master_bytes, master_unit_bytes = old.read('nxl-full040/wake-master'), old.read('nxl-full040/wake-unit')
            r1_data = {}
            if supervised or direct or pkg.name in ("nxl-full040-mnp1", "nxl-full040-yunet1", "nxl-full040-yunet2"):
                for name in (*writers, "wake.sh", "verify-before-capture.sh",
                             "nxl-full040-resume.service", "capture-profile.conf", "collect-resume.sh", "nxl-eye-af.so"):
                    r1_data[name] = old.read("unified-rebuild/nxl-full040-rebuild1/" + name)
        write(root / "opt/usr/nx-lut-lite/state/demo51-installed", "install_revision=release-0.4.0-rc6.14.12-log117\n")
        write(state / "armed", BOOT + '\nEYE\nFAST_START_EYE_AF_ROOTLIB_100_300\n')
        write(state / "launch-claim", "retained-previous-claim\n")
        (state / "capture-claimed").mkdir()
        write(etc / "nxl-full040-resume.service", (HERE.parent / "resume-profile/nxl-full040-resume.service").read_bytes())
        for name in ("install.sh", "nx_cs.adj", "info.tg"):
            write(pkg.parent / name, (pkg / "activation" / name).read_bytes())
        write(pkg / "request", "REBUILD\n")
        ctl = '''#!/bin/sh
echo "ctl $*" >> sequence
case "$*" in
    *di-camera-app.service*) echo 'ExecStart=/bin/sh /usr/local/lib/nxl-full040/launch.sh';;
    *hib_post.service*) if [ -f reject-graph ]; then echo 'Wants='; else echo 'Wants=nxl-full040-resume.service'; fi;;
    *nxl-full040-resume.service*) echo 'ExecStart=/bin/sh /usr/local/lib/nxl-full040/wake.sh';;
    *nxl-yunet-repair.service*) echo 'LoadState=loaded'; if [ -f reject-service ]; then echo 'MainPID=0'; else echo "MainPID=$PPID"; fi;;
esac
'''
        write(root / "usr/bin/systemctl", ctl)
        program = (pkg / "worker.sh").read_text("utf-8").replace("task_root=\n", '''task_root=.
sync(){ :; }
stat(){
    if [ "$1" = -c ] && [ "$2" = %d ]; then
        if [ "$3" = ./opt/usr ]; then echo 14; else echo 10; fi
    else command stat "$@"; fi
}
ln(){ if [ "$1" = -s ]; then printf '%s\\n' "$2" > "$3"; else command ln "$@"; fi; }
readlink(){
    case "$1" in */hib_post.service.wants/nxl-full040-resume.service|*/multi-user.target.wants/nx-on-wake.service) cat "$1";; *) command readlink "$@";; esac
}
''').replace('[ -b "$DEV" ]', '[ -f "$DEV" ]').replace('307199', '8').replace('157285888', '4096')
        # Windows/MSYS cannot create a Linux absolute symlink here. The link syscall
        # is a file endpoint in these worker cases; the real link is exercised in ARM RAM.
        program = program.replace('[ -L "$link" ]', '[ -f "$link" ]')
        program = program.replace('[ -L "$masterwant" ]', '[ -f "$masterwant" ]')
        program = program.replace('cd / || fail FINAL_CWD', 'cd . || fail FINAL_CWD').replace('>/dev/console', '>"$task_root/dev/console"')
        # Change endpoint identities only. Every real runtime script, library and module manifest is compared.
        for path, wanted in re.findall(r'pin "\$task_root([^"\n]*)" ([0-9a-f]{64})', program):
            base = path.rsplit("/", 1)[-1]
            if base in writers and base in r1_data and digest(r1_data[base]) == wanted:
                continue
            dest = root / path.lstrip('/')
            if not dest.exists():
                write(dest, "firmware-endpoint " + path)
            program = program.replace(wanted, digest(dest.read_bytes()))
        core = "b3e7cd2987282d856a8f95b8768d44ea2f4f3804684a391de0567c10dada73c8"
        write(root / "usr/lib/libmmfcore.so", "stock core fixture")
        program = program.replace(core, digest((root / "usr/lib/libmmfcore.so").read_bytes()))
        # The ARM byte-comparison endpoint is replaced by byte-for-byte PC comparison.
        program = program.replace(digest((pkg / "fileops.arm").read_bytes()), digest(OPS.encode()))
        write(pkg / "fileops.arm", OPS)
        write(state / "fileops.arm", OPS)
        for name, replacement in {
            "nxl-menu": '#!/bin/sh\necho notice >> sequence\ntest ! -f reject-notice\n',
            "dispatch.sh": (pkg / "dispatch.sh").read_text().replace("task_root=\n", "task_root=.\n")}.items():
            program = program.replace(digest((pkg / name).read_bytes()), digest(replacement.encode()))
            write(pkg / name, replacement)
            if name == "dispatch.sh" and not primary:
                write(rt / name, replacement)
        if case == "missing-state":
            (state / "armed").unlink()
        elif case == "foreign-runtime":
            write(rt / "launch.sh", "other owner")
        elif case == "bad-module":
            write(root / "opt/usr/nx-lut-lite/process.sh", "changed")
        elif case == "low-battery":
            write(root / "sys/class/power_supply/battery/capacity", "45\n")
        elif case == "busy":
            write(root / "tmp/nx-lut-lite/gamma-r11h.lease", "busy")
        elif case == "foreign-override":
            write(etc / "di-camera-app.service.d/99-foreign.conf", "foreign")
        elif case == "foreign-gate":
            write(etc / "hibernation.service.d/other.conf", "foreign")
        elif case == "foreign-unit":
            write(etc / "hib_post.service", "foreign")
        elif case == "bad-payload":
            write(pkg / "wake.sh", "changed")
        elif case in ("reject-graph", "reject-notice", "yunet1-reject-service"):
            if case == "yunet1-reject-service":
                write(root / "reject-service", "1\n")
            else:
                write(root / case, "1\n")
        elif case == "reject-notice-existing":
            write(root / "reject-notice", "1\n")
        elif case in ("backup-failure", "mnp-backup-failure", "yunet1-backup-failure"):
            write(root / "bin/dd", '#!/bin/sh\nexit 4\n')
            # It is the selected known device-read endpoint, not a firmware mismatch.
            oldpin = re.search(r'pin "\$task_root/bin/dd" ([0-9a-f]{64})', program)[1]
            program = program.replace(oldpin, digest((root / "bin/dd").read_bytes()))
        if r1 or mnp or yunet or primary:
            for name in writers:
                if case != "r1-mixed-writers" or name == "hibernation.sh":
                    write(root / "usr/bin" / name, r1_data[name])
            for name in ("wake.sh", "verify-before-capture.sh", "collect-resume.sh", "nxl-eye-af.so"):
                previous = primary_runtime if primary else mnp_runtime if mnp else yunet_runtime
                write(rt / name, (previous / name).read_bytes() if previous else r1_data[name])
            if case in ("mnp-mixed-runtime", "yunet1-mixed-runtime"):
                write(rt / "wake.sh", r1_data["wake.sh"])
            write(etc / "nxl-full040-resume.service", r1_data["nxl-full040-resume.service"])
            write(gate, r1_data["capture-profile.conf"])
            if case == "r1-foreign-gate":
                write(gate.with_name("99-other.conf"), "foreign")
            if case == "r1-foreign-wake":
                write(rt / "wake.sh", "other owner")
            if case in ("r1-reject-graph", "mnp-reject-graph", "yunet1-reject-graph", "primary-reject-graph"):
                write(root / "reject-graph", "1\n")
        if primary:
            write(rt / "capture-origin", (state / "armed").read_bytes())
            active_rt = rt
            if predecessor:
                active_rt = rt.with_name(predecessor.name)
                active_state = state.with_name(predecessor.name)
                shutil.copytree(state, active_state)
                for name in ("launch.sh", "dispatch.sh", "full-menu.sh", "wake.sh", "collect-resume.sh",
                             "legacy-launch.sh", "legacy-wake.sh", "nxl-eye-af.so"):
                    write(active_rt / name, (predecessor / name).read_bytes())
                write(active_rt / "capture-origin", (active_state / "armed").read_bytes())
                for action in ("launch", "wake"):
                    write(rt / f"{action}.sh", (predecessor / f"{action}-select.sh").read_bytes())
                write(rt / "verify-before-capture.sh", (predecessor / "verify-before-capture.sh").read_bytes())
                write(rt / "readout", (predecessor / "readout").read_bytes())
            if case == "primary-split-active":
                write(active_rt / "capture-origin", "stale capture generation\n")
            if case == "primary-mixed-runtime":
                write(active_rt / "nxl-eye-af.so", b"unrecognized runtime")
        existing_link = r1 or mnp or yunet or primary or case in ("existing-link", "reject-notice-existing")
        if existing_link:
            write(etc / "hib_post.service.wants/nxl-full040-resume.service", "/etc/systemd/system/nxl-full040-resume.service\n")
        existing_master = case in ('existing-master', 'reject-notice-master')
        if existing_master:
            write(root / 'usr/local/bin/nx-on-wake', master_bytes)
            write(etc / 'nx-on-wake.service', master_unit_bytes)
            write(etc / 'multi-user.target.wants/nx-on-wake.service', '/etc/systemd/system/nx-on-wake.service\n')
        if case == 'reject-notice-master':
            write(root / 'reject-notice', '1\n')
        original_state = {p.relative_to(state).as_posix(): p.read_bytes() for p in state.rglob('*') if p.is_file()}
        original_runtime = {p.relative_to(rt).as_posix(): p.read_bytes() for p in rt.rglob('*') if p.is_file()}
        retained = {}
        if predecessor:
            retained = {p: p.read_bytes() for d in (active_rt, active_state) for p in d.rglob('*') if p.is_file()}
        source_image = (root / "dev/mmcblk0p9").read_bytes()
        system_files = [root / "usr/bin" / name for name in writers] + [etc / "nxl-full040-resume.service", gate]
        before = {path: path.read_bytes() if path.is_file() else None for path in system_files}
        write(pkg / "worker.sh", program)
        worker_args = ()
        if supervised or direct:
            leaf = "FAST_START_EYE_AF_ROOTLIB_100_300"
            archived = pkg.parent / "NXDEMO51" / leaf / "entry"
            archived.mkdir(parents=True)
            for name in ("info.tg", "nx_cs.adj", "install.sh"):
                (pkg.parent / name).rename(archived / name)
            worker_args = (leaf,)
        for p in root.rglob('*'):
            if p.is_file(): p.chmod(0o755)
        result = run(root, f"mnt/mmc/{pkg.name}/worker.sh", *worker_args)
        report = next((root / "mnt/mmc/NXDEMO51").glob("FAST_START_EYE_AF_ROOTLIB_*"))
        receipt = (report / "result.txt").read_text()
        trace = (root / "sequence").read_text() if (root / "sequence").exists() else ''
        if (root / "dev/mmcblk0p9").read_bytes() != source_image:
            raise AssertionError("A preparation test changed the simulated snapshot")
        if any(path.read_bytes() != data for path, data in retained.items()):
            raise AssertionError((case, "predecessor state or runtime changed"))
        success = case in ("success", "existing-link", "existing-master", "r1-upgrade", "mnp-upgrade",
                           "yunet1-upgrade", "primary-upgrade")
        if success:
            if result.returncode or "reason=OEM_RESCUE_REQUEST" not in receipt or trace.count("ctl rescue") != 1:
                raise AssertionError((case, result.returncode, receipt, (report / "prepare.log").read_text()[-2000:]))
            if primary:
                new_rt = rt.with_name(pkg.name)
                new_state = state.with_name(pkg.name)
                for name, expected in original_state.items():
                    assert (state / name).read_bytes() == expected, (case, "active state changed", name)
                for name, expected in original_runtime.items():
                    if name not in ("launch.sh", "wake.sh", "verify-before-capture.sh"):
                        assert (rt / name).read_bytes() == expected, (case, "active payload changed", name)
                for name in ("launch", "wake"):
                    assert (rt / (name + ".sh")).read_bytes() == (pkg / (name + "-select.sh")).read_bytes()
                    assert (new_rt / ("legacy-" + name + ".sh")).read_bytes() == original_runtime[name + ".sh"]
                assert (new_rt / "nxl-eye-af.so").read_bytes() == (pkg / "nxl-eye-af.so").read_bytes()
                assert (new_state / "armed").read_bytes() == (new_rt / "capture-origin").read_bytes()
                assert not (new_state / "launch-claim").exists()
                assert all(path.read_bytes() == data for path, data in before.items())
            elif direct:
                if (report / "mmcblk0p9-before.img").exists() or (report / "BACKUP.VERIFIED").exists():
                    raise AssertionError("Direct repair attempted another snapshot backup")
                if not list(rt.glob("nxl-eye-af.so.pre-full040-*")):
                    raise AssertionError("Atomic file replacement lost the old file link")
            elif (report / "mmcblk0p9-before.img").read_bytes() != source_image:
                raise AssertionError("Backup differs")
            if not primary and (not list(state.parent.glob("nxl-full040.previous-*"))
                                or (not direct and not list(rt.parent.glob("nxl-full040.previous-*")))):
                raise AssertionError("Previous generation lost")
            if not primary and (rt / "wake.sh").read_bytes() != (pkg / "wake.sh").read_bytes():
                raise AssertionError("Unified entry not installed")
            if not primary and (rt / "nxl-eye-af.so").read_bytes() != (pkg / "nxl-eye-af.so").read_bytes():
                raise AssertionError("Eye library not installed")
            if (r1 or mnp or yunet) and any(path.read_bytes() != data for path, data in before.items()):
                raise AssertionError("Existing R1 writers or service changed")
            if mnp and "previous_runtime=eye-mnp1" not in (report / "prepare.log").read_text():
                raise AssertionError("Existing MNP runtime was not identified")
            if yunet and "previous_runtime=eye-yunet1" not in (report / "prepare.log").read_text():
                raise AssertionError("Existing YuNet runtime was not identified")
            if existing_master and (etc / 'multi-user.target.wants/nx-on-wake.service').exists():
                raise AssertionError('Legacy master still enabled')
        else:
            if result.returncode == 0 or "ctl rescue" in trace:
                raise AssertionError((case, receipt, trace))
            for name, value in original_state.items():
                if (state / name).read_bytes() != value:
                    raise AssertionError((case, "state rollback", name))
            for name, value in original_runtime.items():
                if (rt / name).read_bytes() != value:
                    raise AssertionError((case, "runtime rollback", name))
            if any((path.read_bytes() if path.is_file() else None) != data for path, data in before.items()):
                raise AssertionError((case, "configuration rollback"))
            if (etc / "hib_post.service.wants/nxl-full040-resume.service").exists() != existing_link:
                raise AssertionError((case, "new dependency survived rollback"))
            if existing_master and not (etc / 'multi-user.target.wants/nx-on-wake.service').exists():
                raise AssertionError('Legacy master not restored')
        if any((pkg.parent / f).exists() for f in ("info.tg", "nx_cs.adj", "install.sh")):
            raise AssertionError((case, "installation entry was left active"))
        expected_failure = {"mnp-mixed-runtime": "INSTALLED_GENERATION_MISMATCH",
                            "mnp-backup-failure": "SNAPSHOT_BACKUP_READ",
                            "mnp-reject-graph": "RESUME_DEPENDENCY_NOT_LOADED",
                            "yunet1-mixed-runtime": "INSTALLED_GENERATION_MISMATCH",
                            "yunet1-backup-failure": "SNAPSHOT_BACKUP_READ",
                            "yunet1-reject-graph": "RESUME_DEPENDENCY_NOT_LOADED",
                            "yunet1-reject-service": "NOT_SYSTEM_SERVICE_MAIN",
                            "primary-split-active": "ACTIVE_GENERATION_MISMATCH",
                            "primary-mixed-runtime": "INSTALLED_GENERATION_MISMATCH",
                            "primary-reject-graph": "RESUME_DEPENDENCY_NOT_LOADED"}.get(case)
        if expected_failure and "reason=" + expected_failure not in receipt:
            raise AssertionError((case, receipt))
        return {"exit": result.returncode, "receipt": receipt.strip(), "snapshot_unchanged": True, "rescue_calls": trace.count("ctl rescue")}


def wake_test(bundle, case, runtime="nxl-full040"):
    with tempfile.TemporaryDirectory(prefix="nx-unified-wake-") as work:
        root = Path(work)
        fixtures(root)
        pkg = bundle / payload_folder(bundle)
        state, rt, run_dir, mod = [root / n for n in (f"opt/usr/{runtime}", f"usr/local/lib/{runtime}", "tmp/nx-lut-lite", "opt/usr/nx-lut-lite")]
        origin = BOOT + '\nEYE\nFAST_START_EYE_AF_ROOTLIB_100_300\n'
        write(state / "armed", origin)
        write(rt / "capture-origin", origin)
        write(root / "proc/sys/kernel/random/boot_id", CAPTURE + '\n')
        write(state / "launch-claim", f"boot_id={CAPTURE}\npolicy=EYE\ncamera_pid=500\nmapped_dev=01:02\nmapped_ino=123\nfile=/usr/local/lib/{runtime}/nxl-eye-af.so\n")
        write(root / "proc/500/maps", f"1000-2000 r-xp 0 01:02 123 /usr/local/lib/{runtime}/nxl-eye-af.so\n")
        write(rt / "nxl-eye-af.so", (pkg / "nxl-eye-af.so").read_bytes())
        process = "nxl_require_normal_mode(){ return 0; }\n"
        if case == "factory-mode":
            process = "nxl_require_normal_mode(){ echo 'Factory script mode (dfmsd) is active.' >&2; return 1; }\n"
        elif case == "process-state-unavailable":
            process = "nxl_require_normal_mode(){ echo 'Cannot inspect factory-mode process state.' >&2; return 1; }\n"
        menu = '''#!/bin/sh
test -f ./tmp/nx-lut-lite/eye-af.resume-ready || exit 3
echo menu >> sequence
test ! -f reject-menu || exit 4
echo 600 > ./tmp/nx-lut-lite/listener.ready
'''
        collector = '#!/bin/sh\necho collector >> sequence\n'
        for name, data in ((state / "fileops.arm", OPS), (rt / "fileops.arm", OPS), (mod / "process.sh", process), (rt / "full-menu.sh", menu), (rt / "collect-resume.sh", collector)):
            write(name, data)
        program = (pkg / "wake.sh").read_text().replace("task_root=\n", '''task_root=.
sleep(){ :; }
readlink(){
    case "$1" in ./proc/500/exe) echo /usr/apps/com.samsung.di-camera-app/bin/di-camera-app;;
    ./proc/600/exe) echo /opt/usr/nx-lut-lite/bin/nxl-keys;; *) command readlink "$@";; esac
}
dmesg(){ echo 'kernel_resume start!'; }
''')
        for name, stub in (("fileops.arm", OPS), ("full-menu.sh", menu), ("collect-resume.sh", collector)):
            program = program.replace(digest((pkg / name).read_bytes()), digest(stub.encode()))
        program = program.replace("cf0f12a1b5eba46ae9ec46c2d3b98ff16aed93582d29d378e157ea5d32589867", digest(process.encode()))
        if case == "camera-missing":
            program = program.replace("echo /usr/apps/com.samsung.di-camera-app/bin/di-camera-app;;", "return 1;;")
        write(root / "tmp/.hib_post", "1\n")
        if case == "no-sd":
            write(root / "proc/mounts", "/dev/root / ext4 ro 0 0\n/dev/mmcblk0p14 /opt/usr ext4 rw 0 0\n")
        elif case == "usb":
            write(root / "sys/class/extcon/USB/state", "1\n")
        elif case == "capturing":
            write(root / "opt/etc/.hib_capturing", "1\n")
        elif case == "wrong-generation":
            write(rt / "capture-origin", "other\nEYE\nold\n")
        elif case == "wrong-map":
            write(root / "proc/500/maps", "1000-2000 r-xp 0 01:02 999 /other\n")
        elif case == "disabled":
            write(mod / "state/disabled", "1\n")
        elif case == "reject-menu":
            write(root / "reject-menu", "1\n")
        elif case == "foreign-token":
            write(run_dir / "eye-af.resume-ready", "999\n")
        write(rt / "wake.sh", program)
        for p in root.rglob('*'):
            if p.is_file(): p.chmod(0o755)
        result = run(root, f"usr/local/lib/{runtime}/wake.sh")
        trace = (root / "sequence").read_text() if (root / "sequence").exists() else ''
        text = (state / "normal-wake.status").read_text() if (state / "normal-wake.status").exists() else ''
        success = case in ("success", "no-sd")
        if success:
            if result.returncode or "EYE_AND_MENU_READY" not in text or trace.count("menu") != 1:
                raise AssertionError((case, result.returncode, text, result.stderr.decode(errors='replace')))
            if (run_dir / "eye-af.resume-ready").read_text() != "500\n": raise AssertionError(case)
        elif case not in ("usb", "capturing"):
            if result.returncode == 0 or ("menu" in trace and case != "reject-menu"): raise AssertionError((case, text))
        elif trace or (run_dir / "eye-af.resume-ready").exists(): raise AssertionError(case)
        expected_failure = {"factory-mode": "FACTORY_MODE_ACTIVE", "process-state-unavailable": "PROCESS_STATE_UNAVAILABLE",
                            "camera-missing": "CAMERA_PROCESS_NOT_READY"}.get(case)
        if expected_failure and ("result=" + expected_failure not in text or (run_dir / "eye-af.resume-ready").exists()):
            raise AssertionError((case, text))
        return {"exit": result.returncode, "eye_and_menu_ready": "EYE_AND_MENU_READY" in text, "menu_calls": trace.count("menu")}


def host_test(bundle, case):
    host = load("nx_unified_host_" + case.replace('-', '_'), bundle / "coexist_rebuild.py")
    with tempfile.TemporaryDirectory(prefix="nx-unified-card-") as work:
        card = Path(work)
        if case == 'failed-camera-preflight':
            report = card / 'NXDEMO51/FAST_START_EYE_AF_ROOTLIB_1234_500'
            write(report / 'prepare.log', 'interface=full040-unified-1\noperation=REBUILD\nSTOPPED=FULL_BATTERY_REQUIRED\n')
            write(report / 'result.txt', 'exit=1\nreason=FULL_BATTERY_REQUIRED\n')
            with patch.object(host, 'identity'):
                result = host.status(card, 0xF45A8A24)
            if result['result'] != 'CAMERA_RECEIPTS' or 'FULL_BATTERY_REQUIRED' not in result['receipts']['result.txt']:
                raise AssertionError('Camera refusal was hidden')
            return {'preflight_refusal_reported_without_capture_marker': True}
        for name in ("DCIM/photo.SRW", "looks/current.cube", "config/native", "ALBM/index", "SYSTEM/native", "nx-old/test", "NXDEMO51/logs/run-RC1.log", "ROLLBACK/install.sh", "personal.txt"):
            write(card / name, "retain:" + name)
        if case == "old-trigger":
            write(card / "info.tg", "old activation")
        if case == "r1-on-card":
            write(card / "nxl-full040-rebuild1/request", "REBUILD\n")
        before = {p.relative_to(card).as_posix(): p.read_bytes() for p in card.rglob('*') if p.is_file()}
        if case == "bad-package":
            with tempfile.TemporaryDirectory(prefix="nx-unified-bad-") as bad:
                bad = Path(bad)
                shutil.copytree(bundle, bad / "bundle")
                write(bad / "bundle" / payload_folder(bundle) / "wake.sh", "broken")
                with patch.object(host, "identity"):
                    try: host.prepare(bad / "bundle", card, 0xF45A8A24)
                    except ValueError as error:
                        if "Package identity mismatch:" not in str(error):
                            raise
                    else: raise AssertionError(case)
                if {p.relative_to(card).as_posix(): p.read_bytes() for p in card.rglob('*') if p.is_file()} != before:
                    raise AssertionError("Invalid package mutated the card")
                return {"invalid_payload_rejected_before_changes": True}
        with patch.object(host, "identity"):
            result = host.prepare(bundle, card, 0xF45A8A24)
        archive = card / result["archive"]
        for name, value in before.items():
            top = name.split('/')[0]
            path = archive / name if top in result["archived_entries"] else card / name
            if path.read_bytes() != value: raise AssertionError((case, name))
        if (card / "NXDEMO51").exists() or (card / "nx-old").exists(): raise AssertionError("Dirty root")
        if result["delete_count"] or not (card / "info.tg").is_file(): raise AssertionError(case)
        return {"archived_entries": result["archived_entries"], "all_original_bytes_retained": True, "delete_count": 0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    results = {"worker": {}, "wake": {}, "host": {}}
    workers = ("success", "existing-link", 'existing-master', "missing-state", "foreign-runtime", "bad-module", "low-battery", "busy", "foreign-override",
               "foreign-gate", "foreign-unit", "bad-payload", "backup-failure", "reject-graph", "reject-notice", "reject-notice-existing", 'reject-notice-master')
    hosts = ("clean-root", "old-trigger", "bad-package", 'failed-camera-preflight')
    if payload_folder(args.bundle) == "nxl-full040-mnp1":
        workers += ("r1-upgrade", "r1-mixed-writers", "r1-foreign-gate", "r1-foreign-wake", "r1-reject-graph")
        hosts += ("r1-on-card",)
    for case in workers:
        results["worker"][case] = worker_test(args.bundle, args.baseline, case)
        print("PASS worker", case, flush=True)
    for case in ("success", "no-sd", "usb", "capturing", "wrong-generation", "wrong-map", "disabled", "reject-menu", "foreign-token"):
        results["wake"][case] = wake_test(args.bundle, case)
        print("PASS wake", case, flush=True)
    for case in hosts:
        results["host"][case] = host_test(args.bundle, case)
        print("PASS host", case, flush=True)
    results["scope"] = "PC filesystem execution only. Storage reads, rescue, camera readiness and key inputs are fixtures. No physical snapshot or camera behavior is claimed."
    args.report.write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n', encoding="utf-8")


if __name__ == '__main__':
    main()

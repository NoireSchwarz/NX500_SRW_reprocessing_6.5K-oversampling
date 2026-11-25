"""Build a camera-specific unified startup replacement from the verified 0.4.0 payload."""
from hashlib import sha256
import io
import json
from pathlib import Path, PurePosixPath
import re
import tarfile
from zipfile import ZipFile, ZIP_DEFLATED, ZipInfo
from importlib.util import spec_from_file_location, module_from_spec

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent
FOLDER = "nxl-full040-rebuild1"
BASELINE_SHA = "d9a7aa9c9f0e66ad5ba25f795d30469de973c57fb03d712488530fdea5768796"
PUBLIC_BASELINE_SHA = "c13582e96444968e5d75b2d276ab2ac6ce173f054cc0971bd15a12b78250f3ec"


def digest(data):
    return sha256(data).hexdigest()


def put(root, name, data):
    member = PurePosixPath(name)
    if member.is_absolute() or '..' in member.parts or '\\' in name or ':' in name:
        raise ValueError('Unsafe output path')
    dest = root / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data.encode() if isinstance(data, str) else data)


def expand(name, values):
    text = (HERE / name).read_text("utf-8")
    for key, value in values.items():
        text = text.replace("@" + key + "@", value)
    if re.search(r"@[A-Z_]+@", text):
        raise ValueError("Unexpanded template in " + name)
    return text.encode()


def build(baseline, destination, original):
    destination = Path(destination)
    if destination.exists():
        raise FileExistsError(destination)
    if digest(Path(baseline).read_bytes()) not in (BASELINE_SHA, PUBLIC_BASELINE_SHA):
        raise ValueError('The baseline is not the verified published 0.4.0 ZIP')
    spec = spec_from_file_location("protected_writers", SOURCE / "capture-check/protected_writers.py")
    writers = module_from_spec(spec)
    spec.loader.exec_module(writers)
    # All existing camera payload identities come from the published, CRC-verified package.
    with ZipFile(baseline) as archive:
        if archive.testzip():
            raise ValueError("Baseline ZIP CRC failure")
        pins = json.loads(archive.read("checksums.json"))
        old = {name.split("/", 1)[1]: archive.read(name) for name in pins}
        if any(digest(old[n.split('/', 1)[1]]) != pin for n, pin in pins.items()):
            raise ValueError("Baseline payload mismatch")
        if digest(old["nxl-eye-af.so"]) != "e167e84a6be88b2fc21da13a5952a923ec7a032cb37612a11e047e60dc6c2e6e":
            raise ValueError("Not the supported Eye AF v2.8 baseline")
        manifests = ["e3520acc8a3adf788cf6bf1a5a9a927ee55ab5fd6cf296ffc35ca79286baa167",
                     digest(archive.read("module/nx-lut-demo/module.manifest"))]
    data = {name: old[name] for name in ("fileops.arm", "claim.arm", "nxl-menu", "nxl-eye-af.so", "launch.sh", "dispatch.sh", "readout", "full-menu.sh")}
    data["lcd-observe.arm"] = old["lcd-observe.arm"]
    # Retain the proven collector; remove its menu dispatch and all startup authority.
    text = old["readout"].decode()
    start = text.index("# An early mount may precede hib_post/swapoff.")
    data["collect-resume.sh"] = (text[:start].replace("$SD/nxl-full040/lcd-observe.arm", "$SD/" + FOLDER + "/lcd-observe.arm") +
        'normal_boot && card_ready && collect\n').encode()
    data["wake.sh"] = expand("wake.sh", {"OPS": digest(data["fileops.arm"]), "PROCESS": "cf0f12a1b5eba46ae9ec46c2d3b98ff16aed93582d29d378e157ea5d32589867",
        "MENU": digest(data["full-menu.sh"]), "EYE": digest(data["nxl-eye-af.so"]), "COLLECTOR": digest(data["collect-resume.sh"])})
    data["nxl-full040-resume.service"] = (HERE / "nxl-full040-resume.service").read_bytes()
    checker = (SOURCE / "capture-check/verify-before-capture.sh").read_text("utf-8").replace(
        "helper=/opt/usr/nxl-full040/resume-after-stock.sh", "helper=/usr/local/lib/nxl-full040/wake.sh")
    root_pins = {"usr/local/lib/nxl-full040/" + name: digest(data[name]) for name in
                 ("fileops.arm", "wake.sh", "full-menu.sh", "nxl-eye-af.so", "launch.sh")}
    root_pins["etc/systemd/system/nxl-full040-resume.service"] = digest(data["nxl-full040-resume.service"])
    extra = ('old_master=$root/etc/systemd/system/multi-user.target.wants/nx-on-wake.service\n'
             '[ ! -e "$old_master" ] && [ ! -L "$old_master" ] || stop OLD_WAKE_MASTER_STILL_ENABLED\n'
             'ops=$root/usr/local/lib/nxl-full040/fileops.arm\n')
    for name, pin in root_pins.items():
        extra += f'[ -f "$root/{name}" ] && [ ! -L "$root/{name}" ] && [ "$("$ops" sha256 "$root/{name}")" = {pin} ] || stop UNIFIED_PAYLOAD_MISMATCH\n'
    extra += '''origin=$root/usr/local/lib/nxl-full040/capture-origin
[ -f "$origin" ] && [ ! -L "$origin" ] || stop CAPTURE_ORIGIN_MISSING
{ IFS= read -r old_boot && IFS= read -r policy && IFS= read -r report; } < "$origin" || stop CAPTURE_ORIGIN_MISSING
[ -n "$old_boot" ] && [ "$old_boot" != "$boot" ] && [ "$policy" = EYE ] || stop CAPTURE_ORIGIN_INVALID
suffix=${report#FAST_START_EYE_AF_ROOTLIB_}
case "$suffix" in "$report"|''|*[!0-9_]*) stop CAPTURE_ORIGIN_INVALID;; esac
printf 'interface=full040-unified-1\\norigin_boot_id=%s\\ncapture_report=%s\\n' "$old_boot" "$report"
'''
    anchor = "printf 'decision=PASS\\nreason=CAPTURE_MANAGER_REFERENCES_INSTALLED_WAKE\\n'"
    if checker.count(anchor) != 1:
        raise ValueError("Unknown prewrite checker boundary")
    data["verify-before-capture.sh"] = checker.replace(anchor, extra + anchor).encode()
    data["capture-profile.conf"] = (SOURCE / "capture-check/capture-profile.conf").read_bytes()
    for path in writers.PIN:
        data[Path(path).name] = writers.protected_source(path, original(path))
    old_unit = (SOURCE / "resume-profile/nxl-full040-resume.service").read_bytes()
    # Preserve every recognized predecessor override; a foreign override stops installation.
    allowed = {"50-nxl-eye-af.conf": "714b38ab2d1b7f6841deb0b4686be0959215f4ac822c3ae22f55f43f85fe1c19",
        "60-nxl-eye-faststart.conf": "060441c5617b47a98dfbd57095737ce3008649420c11e1babc4fe6ca1939896d",
        "61-nxl-eye-image.conf": "6acb8bede9a44cc89cd14049015eb9f926e04416e4e84facbc1b68a7fb4f2c44",
        "62-nxl-eye-priority.conf": "0ff3114fdba7278e517eedf47b39623d7e4742135d6b35617bcac874495a28f3",
        "63-nxl-eye-reacquire.conf": "f26abdac599dd0889639afa7a6b6b2434cefe9714987280ec8cc001a908f46d6",
        "64-nxl-eye-motion.conf": "700ed77e70e86624fe1d158dbc77eb61b5a714fc9c756fee849c30246ab1dec1",
        "65-nxl-eye-face-return.conf": "7fd3ef8f7ad4b2e91e7577766cd424f00ac90ff52ed587d7ee1f161b0411dcb4",
        "66-nxl-eye-relock.conf": "32afa1a259b228d71b14cb32438a06023bc7afeed56c8fe91e2735f1865ef1ae",
        "67-nxl-eye-publication.conf": "c2a59b6af797805d893addc6313e4caf25b196002bc499bd055585d2d84ca3f7",
        "68-nxl-full040.conf": digest(old["capture.conf"])}
    data["allowed-overrides.txt"] = ("".join(f"{name} {pin}\n" for name, pin in sorted(allowed.items())) +
        "61-nxl-eye-image.conf 8377f036ef07652a0d8c8662bf5e7bc9414233eb60a4cad65d6eda874fcbd4ee\n").encode()
    stock_paths = ["/usr/bin/systemctl", "/usr/bin/hibernation.sh", "/usr/bin/make_snapshot.sh",
        "/usr/lib/libhibernation.so", "/usr/lib/systemd/system/di-camera-app.service",
        "/usr/apps/com.samsung.di-camera-app/bin/di-camera-app", "/usr/lib/libcapture-fw-prod.so", "/usr/lib/libface.so",
        "/usr/lib/systemd/system/hibernation.service", "/usr/lib/systemd/system/hib_post.service", "/usr/lib/systemd/system/hib_post.path",
        "/bin/tar", "/bin/dd", "/bin/cp"]
    stock_pins = {name: digest(original(name)) for name in stock_paths}
    checks = '\n'.join(f'pin "$task_root{name}" {pin} || fail STOCK_IDENTITY_CHANGED' for name, pin in stock_pins.items())
    core = 'core=$("$OPS" sha256 "$task_root/usr/lib/libmmfcore.so") || fail CORE_UNREADABLE\ncase "$core" in b3e7cd2987282d856a8f95b8768d44ea2f4f3804684a391de0567c10dada73c8|7743dd26b06fb2e600cc65aff6ad92913b43c3f75b9493437239006b24991cd9) ;; *) fail CORE_UNRECOGNIZED;; esac'
    installed = {"$RT/" + name: digest(old[name]) for name in ("launch.sh", "readout", "dispatch.sh", "full-menu.sh", "nxl-eye-af.so")}
    installed.update({"$STATE/" + name: digest(old[name]) for name in ("fileops.arm", "claim.arm")})
    installed["$ETC/di-camera-app.service.d/68-nxl-full040.conf"] = digest(old["capture.conf"])
    payload = '\n'.join(f'pin "$PKG/{name}" {digest(value)} || fail PAYLOAD_IDENTITY' for name, value in sorted(data.items()))
    data["worker.sh"] = expand("worker.sh", {"PAYLOAD_CHECKS": payload, "STOCK_CHECKS": checks + '\n' + core,
        "INSTALLED_CHECKS": '\n'.join(f'pin "{name}" {pin} || fail INSTALLED_GENERATION_MISMATCH' for name, pin in installed.items()),
        "MANIFESTS": '|'.join(manifests), "OLD_UNIT": digest(old_unit),
        "OLD_MASTER": digest(old['wake-master']), "OLD_MASTER_UNIT": digest(old['wake-unit'])})
    data["activation/install.sh"] = (f'''#!/bin/sh
trap '' HUP
PKG=/mnt/mmc/{FOLDER}
OPS=$PKG/fileops.arm
[ -f "$OPS" ] && [ ! -L "$OPS" ] && [ "$("$OPS" sha256 "$OPS")" = {digest(data['fileops.arm'])} ] || exit 70
[ -f "$PKG/worker.sh" ] && [ ! -L "$PKG/worker.sh" ] && [ "$("$OPS" sha256 "$PKG/worker.sh")" = {digest(data['worker.sh'])} ] || exit 70
exec /bin/sh "$PKG/worker.sh"
''').encode()
    for name in ("info.tg", "nx_cs.adj"):
        data["activation/" + name] = old["activation/" + name]
    destination.mkdir(parents=True)
    for name, value in data.items():
        put(destination, FOLDER + "/" + name, value)
    put(destination, "coexist_rebuild.py", (HERE / "coexist_rebuild.py").read_bytes())
    put(destination, "rebuild-checksums.json", json.dumps({FOLDER + "/" + name: digest(value) for name, value in sorted(data.items())}, indent=2) + '\n')
    put(destination, "build-provenance.json", json.dumps({"interface": "full040-unified-1", "baseline_zip_sha256": digest(Path(baseline).read_bytes()),
        "stock_originals": stock_pins, "eye_algorithm_unchanged": True, "full_module_unchanged": True,
        "physical_camera_acceptance": "pending", "capture_method": "OEM systemctl rescue; guarded hibernation.sh; no direct image erasure"}, indent=2) + '\n')
    put(destination, "README_zh.md", (HERE / "README_zh.md").read_bytes())
    put(destination, "prepare_interactive.py", (HERE / "prepare_interactive.py").read_bytes())
    put(destination, "PREPARE_SD.cmd", '@echo off\r\ncd /d "%~dp0"\r\npython -X utf8 prepare_interactive.py\r\npause\r\n')
    with ZipFile(baseline) as archive:
        for name in archive.namelist():
            if not name.endswith('/') and (name.startswith(('eye-v2.8/licenses/', 'eye-v2.8/source/')) or name == 'eye-v2.8/NOTICE.md'):
                put(destination, name, archive.read(name))
    return data


def archive(folder, output):
    with ZipFile(output, "x", compression=ZIP_DEFLATED, compresslevel=6) as archive:
        for file in sorted(Path(folder).rglob("*")):
            if not file.is_file() or "__pycache__" in file.parts:
                continue
            data = file.read_bytes()
            info = ZipInfo(file.relative_to(folder).as_posix(), (2026, 11, 25, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (0o100755 if data.startswith((b"#!", b"\x7fELF")) else 0o100644) << 16
            archive.writestr(info, data, compress_type=ZIP_DEFLATED, compresslevel=6)
    with ZipFile(output) as check:
        if check.testzip() or len(check.namelist()) != len(set(check.namelist())):
            raise ValueError("Final ZIP failed CRC or duplicate-name verification")
        for name in check.namelist():
            if check.read(name) != (Path(folder) / name).read_bytes():
                raise ValueError("Final ZIP differs from the verified tree")
    Path(output).with_suffix(".sha256").write_text(digest(Path(output).read_bytes()) + '  ' + Path(output).name + '\n', encoding="ascii")

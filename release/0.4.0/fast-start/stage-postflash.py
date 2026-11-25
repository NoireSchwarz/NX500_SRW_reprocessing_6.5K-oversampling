"""Stage one explicit post-flash operation from the extracted package."""

import argparse
import ctypes
import hashlib
from importlib.util import module_from_spec, spec_from_file_location
import json
import os
from pathlib import Path
import re
import shutil
import tempfile


if not __debug__:
    raise SystemExit("Run with assertions enabled. No files were staged.")

ROOT = Path(__file__).resolve().parent
OLD_READOUT = "76045deea9d0ec98eb81f1909bebecd67398a8d1d1afdc0124898f1840c35aa5"
V1_READOUT = "03c2b9a1babb4cd4a4f6bdced3907addfc2b069638b0dcd272a39f460e007dc3"
V22_WAKE = "55f0d6022306dd6fd9cac207413e20a1e7076f8cb0c7512fa6ab860504193016"
V23_WAKE = "9a589e922e69812a0905f593fc8c71f226b57f0e2acb297e77de5cc1e77d2bd4"
V24_WAKE = "3dc969d2171bcb8872d83e3b8284382074551597b83948f68e321fbcbe56a66b"
V25_WAKE = "db34c06e77e570a3a3f403c9454af627d111cad0443be7d927d5974af73ddf4e"
V26_WAKE = "209e9792389b4679cf6ca3b2ac134161bca63f85fcb79019f76472de6d48d2b6"
V26_ADAPTER = "665b2dd10546f9030a9340563f9e43fb8bf1577c91df08f7eaaeadb90d0f6cc4"
V26_PROBES = {
    "nx-eye-af-box-v26-once": "388dcb15d058c16587fcfbecda613833937a6f16af332874709a159745f5ced5",
    "nx-eye-af-box-v26-read2": "b2ab4d0ec6006cc5113419505d92fcc99e44aec1c94843565d99516c3661cb63",
}
V27_WAKE = "c48be208b3a60691234ae85e918e1e1eeef6b58c9207fddfa6fa688d5e35be52"
FOLDER = "nxl-full040"


def sha(path):
    with path.open("rb") as stream:
        digest = hashlib.sha256()
        for data in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(data)
    return digest.hexdigest()


def identity(card, expected):
    serial = ctypes.c_ulong()
    fs_name = ctypes.create_unicode_buffer(64)
    get_volume = ctypes.WinDLL("kernel32", use_last_error=True).GetVolumeInformationW
    get_volume.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                           ctypes.c_void_p, ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_ulong]
    assert get_volume(str(card), None, 0, ctypes.byref(serial), None, None, fs_name, len(fs_name)), ctypes.get_last_error()
    assert serial.value == expected and fs_name.value.casefold() == "exfat", (card, hex(serial.value), fs_name.value)


def atomic_copy(source, dest, digest, published=None):
    assert not dest.exists() and not dest.is_symlink()
    handle, name = tempfile.mkstemp(prefix=".nxl-image-", dir=dest.parent)
    temp = Path(name)
    try:
        with os.fdopen(handle, "wb") as out, source.open("rb") as src:
            shutil.copyfileobj(src, out)
            out.flush()
            os.fsync(out.fileno())
        assert sha(temp) == digest and not dest.exists() and not dest.is_symlink()
        temp.rename(dest)
        if published is not None:
            published.append(dest.name)
        assert sha(dest) == digest
    finally:
        temp.unlink(missing_ok=True)


def wake_entries(folder):
    # Use each directory entry's own metadata. Reopening by name on exFAT
    # would report the first duplicate's size for both entries.
    with os.scandir(folder) as entries:
        return sorted((e.name, e.is_file(follow_symlinks=False), e.stat(follow_symlinks=False).st_size) for e in entries)


def resume_v26_wake(card, pkg, serial):
    live = card / "nx-on-wake"
    retired = pkg / "previous-diagnostic-wake"
    fresh = pkg / "prepared-v26-wake"
    for path, parent in ((pkg, card), (live, card), (retired, pkg), (fresh, pkg)):
        assert not path.is_symlink() and path.resolve().parent == parent.resolve()
    assert pkg.name == FOLDER
    if live.exists() and len(items := wake_entries(live)) <= 1 and not retired.exists():
        if items != [("on-wake", True, 2805)] and items != [("on-wake", True, 2806)]:
            return
    if not live.exists() and not retired.exists():
        return
    normal = card / "nxl-eye-image26/wake-entry"
    assert sha(normal) == V26_WAKE and sha(normal.with_name("nxl-eye-af.so")) == V26_ADAPTER
    if retired.exists() and live.is_dir() and wake_entries(live) == [("on-wake", True, normal.stat().st_size)]:
        assert sha(live / "on-wake") == V26_WAKE
        return
    source = retired if retired.exists() else live
    entries = wake_entries(source)
    known = []
    for name, pin in V26_PROBES.items():
        probe = card / name / "on-wake"
        assert sha(probe) == pin
        known.append(("on-wake", True, probe.stat().st_size))
    assert entries and len(set(entries)) == len(entries) and all(e in known for e in entries)
    assert sha(source / "on-wake") in V26_PROBES.values()
    fresh.mkdir(exist_ok=True)
    if not (fresh / "on-wake").exists():
        atomic_copy(normal, fresh / "on-wake", V26_WAKE)
    assert wake_entries(fresh) == [("on-wake", True, normal.stat().st_size)] and sha(fresh / "on-wake") == V26_WAKE
    identity(card, serial)
    if source == live:
        live.rename(retired)
    try:
        assert not live.exists() and wake_entries(retired) == entries
        fresh.rename(live)
        assert wake_entries(live) == [("on-wake", True, normal.stat().st_size)] and sha(live / "on-wake") == V26_WAKE
    except BaseException:
        identity(card, serial)
        if not live.exists() and retired.is_dir():
            retired.rename(live)
        raise


def stage(card, serial, policy):
    identity(card, serial)
    if policy in ("REBUILD", "REBUILD-PLAN", "REBUILD-STATUS"):
        bundle = ROOT / "unified-rebuild"
        host = bundle / "coexist_rebuild.py"
        if not host.is_file():
            raise SystemExit("Use the complete Unified R1 release ZIP, which includes unified-rebuild/.")
        spec = spec_from_file_location("nx500_unified_card", host)
        manager = module_from_spec(spec)
        spec.loader.exec_module(manager)
        manager.identity = identity
        result = (manager.prepare(bundle, card, serial) if policy == "REBUILD" else
                  manager.plan(card, serial) if policy == "REBUILD-PLAN" else manager.status(card, serial))
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return
    if policy in ("SIMPLE-WAKE", "SIMPLE-STATUS", "SIMPLE-RESTORE"):
        from simple_wake import manage
        result = manage(card, policy, lambda: identity(card, serial))
        print(json.dumps(result, indent=2))
        return
    pins = json.loads((ROOT / "checksums.json").read_text("utf-8"))
    wake_dir = card / "nx-on-wake"
    active = wake_dir / "on-wake"
    known_wake = pins[FOLDER + "/wake-entry"]
    existing_eye = (policy == "EYE" and not wake_dir.is_symlink() and
                    active.is_file() and not active.is_symlink() and sha(active) == known_wake)
    if policy == "REBUILD-CHECK" or existing_eye:
        from rebuild_evidence import snapshot_evidence
        try:
            result = snapshot_evidence(card, known_wake)
        except (OSError, ValueError) as problem:
            raise SystemExit(f"Existing snapshot evidence unavailable: {problem}") from problem
        identity(card, serial)
        print(json.dumps(result, indent=2))
        # Inspection can succeed even when permission to dispatch the writer
        # is absent. A caller checking only the exit status must also stop.
        raise SystemExit(3)
    for name, digest in pins.items():
        assert name.startswith(FOLDER + "/") and ".." not in Path(name).parts
        assert sha(ROOT / name) == digest
    stock_entries = ("install.sh", "nx_cs.adj", "info.tg")
    for name in (*stock_entries, "nx-lut-demo/eye-af-bt-request"):
        assert not (card / name).exists() and not (card / name).is_symlink(), f"Existing entry: {name}"
    pkg = card / FOLDER
    assert not pkg.is_symlink() and not wake_dir.is_symlink()
    pkg.mkdir(exist_ok=True)
    for name, digest in pins.items():
        dest = card / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        assert dest.parent.resolve() == card.resolve() / Path(name).parent and not dest.is_symlink()
        if dest.exists():
            assert sha(dest) == digest, name
        else:
            atomic_copy(ROOT / name, dest, digest)

    if policy == "EYE":
        resume_v26_wake(card, pkg, serial)
    wake_dir.mkdir(exist_ok=True)
    assert len(wake_entries(wake_dir)) == len({name.casefold() for name, _, _ in wake_entries(wake_dir)}), "Ambiguous wake directory"
    # Keep a working entry in place until the camera has verified the new library.
    keep_live = {V22_WAKE, V23_WAKE, V24_WAKE, V25_WAKE, V26_WAKE, V27_WAKE, "d3e7e697640056ee02fea543ebfb26a53c5d607530b83ea064fac19776e1e4f0"}
    if policy == "CLEAR":
        keep_live.add(pins[FOLDER + "/wake-entry"])
    baseline_live = active.is_file() and not active.is_symlink() and sha(active) in keep_live
    if policy != "INSPECT" and not baseline_live and (active.exists() or active.is_symlink()):
        allowed = {OLD_READOUT, V1_READOUT}
        assert not active.is_symlink() and sha(active) in allowed, "Unknown wake entry preserved"
        retired = Path(tempfile.mkdtemp(prefix="prior-sd-entry-", dir=pkg))
        active.rename(retired / "on-wake")
    request = pkg / "request"
    if request.exists():
        assert request.is_file() and not request.is_symlink()
        retired = Path(tempfile.mkdtemp(prefix="prior-request-", dir=pkg))
        request.rename(retired / "request")
    token = (policy + "\n").encode()
    with request.open("xb") as stream:
        stream.write(token)
        stream.flush()
        os.fsync(stream.fileno())
    assert request.read_bytes() == token
    identity(card, serial)
    published = []
    try:
        for name in stock_entries:
            leaf = f"{FOLDER}/activation/{name}"
            atomic_copy(ROOT / leaf, card / name, pins[leaf], published)
        identity(card, serial)
        assert all(sha(card / name) == pins[f"{FOLDER}/activation/{name}"] for name in published)
    except BaseException:
        identity(card, serial)
        archived = Path(tempfile.mkdtemp(prefix="incomplete-entry-", dir=pkg))
        for name in reversed(published):
            target = card / name
            if target.is_file() and not target.is_symlink() and sha(target) == pins[f"{FOLDER}/activation/{name}"]:
                target.rename(archived / name)
        raise
    print(f"Verified {policy} on {card} ({serial:08X}); info.tg was written last.")


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("policy", choices=("CHECK", "CONTROL", "EYE", "CLEAR", "INSPECT", "REBUILD-CHECK", "REBUILD", "REBUILD-PLAN", "REBUILD-STATUS",
                                       "SIMPLE-WAKE", "SIMPLE-STATUS", "SIMPLE-RESTORE", "BUILD-RESUME-IMAGE"))
    cli.add_argument("--drive", help="For example, H:")
    cli.add_argument("--serial", help="Expected eight-digit exFAT volume serial")
    cli.add_argument("--image-partitions", type=Path, nargs=2, metavar=("PLATFORM_TAR", "OPT_USR_TAR"),
                     help="Existing filesystem archives of the combined release")
    cli.add_argument("--image-output", type=Path, help="New local directory for the two prepared filesystem archives")
    args = cli.parse_args()
    if args.policy == "BUILD-RESUME-IMAGE":
        if not args.image_partitions or not args.image_output or args.drive or args.serial:
            cli.error("BUILD-RESUME-IMAGE requires --image-partitions and --image-output")
        source = ROOT.parent if ROOT.name == "fast-start" and not (ROOT / "resume_profile.py").is_file() else ROOT
        spec = spec_from_file_location("nx500_resume_profile", source / "resume_profile.py")
        profile = module_from_spec(spec)
        spec.loader.exec_module(profile)
        data = profile.compose_images(source, *args.image_partitions, args.image_output)
        print(json.dumps({"prepared_images": ["platform.tar", "opt-usr.tar"],
                          "profile_sha256": hashlib.sha256(data).hexdigest(),
                          "physical_camera_installation": "not_performed"}, indent=2))
        raise SystemExit(0)
    if not args.drive or not args.serial or args.image_partitions or args.image_output:
        cli.error("The selected card operation requires --drive and --serial")
    if re.fullmatch(r"[0-9a-fA-F]{4}-[0-9a-fA-F]{4}", args.serial):
        args.serial = args.serial.replace("-", "")
    assert os.name == "nt" and args.drive[:2].casefold() != os.environ.get("SystemDrive", "").casefold()
    assert re.fullmatch(r"[D-Z]:[/\\]?", args.drive, re.I) and re.fullmatch(r"[0-9a-fA-F]{8}", args.serial)
    stage(Path(args.drive[:2] + "/"), int(args.serial, 16), args.policy)

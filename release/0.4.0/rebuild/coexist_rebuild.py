"""Prepare the unified 0.4.0 rebuild and archive old SD root entries without deleting them."""
import argparse
import ctypes
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import tempfile


FOLDER = "nxl-full040-rebuild1"
TRIGGERS = ("install.sh", "nx_cs.adj", "info.tg")
PRESERVE = {"dcim", "albm", "config", "system", "looks", "system volume information"}


def sha(path):
    with Path(path).open("rb") as stream:
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def identity(card, serial):
    if os.name != "nt" or not re.fullmatch(r"[D-Z]:[/\\]", str(card), re.I):
        raise ValueError("An explicit removable-card root is required")
    value = ctypes.c_ulong()
    filesystem = ctypes.create_unicode_buffer(64)
    get = ctypes.WinDLL("kernel32", use_last_error=True).GetVolumeInformationW
    get.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                    ctypes.c_void_p, ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_ulong]
    if not get(str(card), None, 0, ctypes.byref(value), None, None, filesystem, 64):
        raise OSError(ctypes.get_last_error(), "Cannot read SD identity")
    if value.value != serial or filesystem.value.casefold() != "exfat":
        raise ValueError("The card serial or filesystem differs from the selected card")


def root_entry(card, name):
    """Every rename is between checked descendants of this one SD root."""
    if not name or name in (".", "..") or any(x in name for x in ("/", "\\", ":")):
        raise ValueError("Not a direct root entry")
    path = card / name
    if path.is_symlink() or path.resolve().parent != card.resolve():
        raise ValueError(f"Unsafe SD entry: {name}")
    return path


def archive_candidate(name):
    lower = name.casefold()
    return (lower.startswith(("nx-", "nxl-")) or lower in {"nxdemo51", "rollback", "uninstall", "nx500.bin", *TRIGGERS}
            or bool(re.fullmatch(r"\d{8}_\d{6}_.*\.info", lower)))


def plan(card, serial):
    identity(card, serial)
    rows, names = [], set()
    with os.scandir(card) as entries:
        for entry in entries:
            key = entry.name.casefold()
            if key in names:
                raise ValueError("Ambiguous duplicate SD root entries; no changes made")
            names.add(key)
            root_entry(card, entry.name)
            if key in PRESERVE or key == FOLDER.casefold() or key.startswith("nx500_archive_"):
                action = "preserve"
            else:
                action = "archive" if archive_candidate(entry.name) else "preserve_unknown"
            meta = entry.stat(follow_symlinks=False)
            rows.append({"name": entry.name, "action": action, "directory": entry.is_dir(follow_symlinks=False),
                         "size": meta.st_size, "mtime_ns": meta.st_mtime_ns})
    return {"card": str(card), "serial": f"{serial:08X}", "delete_count": 0,
            "entries": sorted(rows, key=lambda row: row["name"].casefold()),
            "archive_policy": "Existing development directories are retained intact; no filesystem repair or permanent deletion is performed"}


def copy_new(source, dest, digest):
    if dest.exists() or dest.is_symlink():
        raise FileExistsError(dest)
    handle, filename = tempfile.mkstemp(prefix=".unified-", dir=dest.parent)
    temporary = Path(filename)
    try:
        with os.fdopen(handle, "wb") as stream, source.open("rb") as original:
            shutil.copyfileobj(original, stream)
            stream.flush()
            os.fsync(stream.fileno())
        if sha(temporary) != digest or dest.exists() or dest.is_symlink():
            raise ValueError("Atomic copy verification failed")
        temporary.rename(dest)
        if sha(dest) != digest:
            raise ValueError("Published file differs")
    finally:
        temporary.unlink(missing_ok=True)


def validate_bundle(bundle):
    pins = json.loads((bundle / "rebuild-checksums.json").read_text("utf-8"))
    for name, wanted in pins.items():
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name or not name.startswith(FOLDER + "/"):
            raise ValueError("Unsafe package manifest")
        if sha(bundle / name) != wanted:
            raise ValueError(f"Package identity mismatch: {name}")
    for name in (*TRIGGERS, "worker.sh", "fileops.arm", "wake.sh"):
        suffix = "activation/" + name if name in TRIGGERS else name
        if FOLDER + "/" + suffix not in pins:
            raise ValueError(f"Incomplete package: {name}")
    return pins


def prepare(bundle, card, serial):
    pins = validate_bundle(bundle)
    baseline = plan(card, serial)
    target = root_entry(card, FOLDER)
    if target.exists():
        raise FileExistsError("A unified rebuild package is already on the card; inspect STATUS before preparing another")
    archive_name = "NX500_ARCHIVE_" + datetime.now().strftime("%Y%m%d_%H%M%S")
    archive = root_entry(card, archive_name)
    if archive.exists():
        raise FileExistsError(archive)
    temporary = Path(tempfile.mkdtemp(prefix=FOLDER + ".staging-", dir=card))
    moved, published = [], []
    archive.mkdir()
    (archive / "root-before.json").write_text(json.dumps(baseline, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    try:
        for name, wanted in pins.items():
            dest = temporary / name.split("/", 1)[1]
            dest.parent.mkdir(parents=True, exist_ok=True)
            copy_new(bundle / name, dest, wanted)
        # Move whole directories on the same exFAT volume. Do not traverse or delete corrupt historical logs.
        for row in baseline["entries"]:
            if row["action"] != "archive":
                continue
            identity(card, serial)
            source = root_entry(card, row["name"])
            dest = archive / row["name"]
            if dest.resolve().parent != archive.resolve() or dest.exists() or dest.is_symlink():
                raise ValueError("Archive path collision")
            meta = source.stat()
            if meta.st_mtime_ns != row["mtime_ns"] or source.is_dir() != row["directory"]:
                raise ValueError(f"Root changed during preparation: {source.name}")
            source.rename(dest)
            moved.append(row["name"])
            if source.exists() or not dest.exists():
                raise ValueError("Archive rename verification failed")
        identity(card, serial)
        temporary.rename(target)
        with (target / "request").open("xb") as stream:
            stream.write(b"REBUILD\n")
            stream.flush()
            os.fsync(stream.fileno())
        for name in TRIGGERS:  # info.tg is the final publication point.
            identity(card, serial)
            key = FOLDER + "/activation/" + name
            copy_new(bundle / key, card / name, pins[key])
            published.append(name)
        identity(card, serial)
        for name, wanted in pins.items():
            if sha(card / name) != wanted:
                raise ValueError(f"SD package readback failed: {name}")
        receipt = {"result": "UNIFIED_REBUILD_STAGED", "camera_capture_completed": False,
                   "archived_entries": moved, "archive": archive_name, "delete_count": 0,
                   "root_after": sorted(x.name for x in card.iterdir()), "serial": f"{serial:08X}"}
        (archive / "preparation.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        return receipt
    except BaseException:
        # Remove activation by retaining our own published files in the archive.
        identity(card, serial)
        for name in reversed(published):
            dest = card / name
            if dest.is_file() and not dest.is_symlink() and sha(dest) == pins[FOLDER + "/activation/" + name]:
                dest.rename(archive / ("incomplete-" + name))
        (archive / "incomplete.json").write_text(json.dumps({"moved": moved, "activation_withdrawn": True}) + "\n", encoding="utf-8")
        # Preserve the archive and partial package for inspection; no recursive deletion or silent reactivation.
        raise


def status(card, serial):
    identity(card, serial)
    reports = card / "NXDEMO51"
    candidates = []
    if reports.is_dir() and not reports.is_symlink():
        for entry in reports.iterdir():
            if not re.fullmatch(r"FAST_START_EYE_AF_ROOTLIB_\d+_\d+", entry.name) or entry.is_symlink():
                continue
            # Preparation can refuse before the rescue marker exists (battery,
            # file identity, backup, etc.). Report that refusal instead of claiming
            # the camera never executed this package.
            for name in ('CAPTURE_REQUESTED', 'prepare.log'):
                path = entry / name
                if path.is_file() and not path.is_symlink():
                    with path.open('r', encoding='utf-8', errors='replace') as stream:
                        token = stream.read(4096)
                    if 'interface=full040-unified-1\n' in token:
                        candidates.append(entry)
                        break
    if not candidates:
        return {"result": "AWAITING_CAMERA_EXECUTION", "camera_capture_completed": False}
    latest = max(candidates, key=lambda x: tuple(int(v) for v in x.name.rsplit("_", 2)[1:]))
    reads = {}
    for name in ("result.txt", "BACKUP.VERIFIED", "resume/normal-start.txt", "resume/full-menu.txt", "resume/runtime.log"):
        path = latest / name
        if path.is_file() and not path.is_symlink():
            reads[name] = path.read_text("utf-8", errors="replace")
    proofs = []
    for p in (latest / "resume").glob("*.txt"):
        if p.name.startswith(("before-capture.", "image-header.")) and p.is_file() and not p.is_symlink():
            text = p.read_text("utf-8")
            proofs.append({"name": p.name, "text": text})
    def field(text, name):
        values = [line.partition('=')[2] for line in text.splitlines() if line.startswith(name + '=')]
        return values[0] if len(values) == 1 else None
    current = field(reads.get('resume/normal-start.txt', ''), 'boot_id')
    valid = [p['name'] for p in proofs if current and field(p['text'], 'boot_id') == current and
             field(p['text'], 'capture_report') == latest.name and field(p['text'], 'decision') == 'PASS' and
             field(p['text'], 'interface') == 'full040-unified-1']
    gates_ok = any(n.startswith('before-capture.') for n in valid) and any(n.startswith('image-header.') for n in valid)
    backup = latest / 'mmcblk0p9-before.img'
    backup_hash = sha(backup) if backup.is_file() and not backup.is_symlink() and backup.stat().st_size == 157285888 else None
    return {"result": "CAMERA_RECEIPTS", "report": latest.name, "receipts": reads, "capture_proofs": proofs,
            "snapshot_backup_sha256_computed_on_PC": backup_hash,
            "same_generation_prewrite_verified": gates_ok,
            "runtime_eye_and_menu_ready": field(reads.get('resume/normal-start.txt', ''), 'result') == 'EYE_AND_MENU_READY',
            "physical_eye_and_menu_acceptance": "User must test both in the same ordinary cold start with installation triggers absent"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("PLAN", "PREPARE", "STATUS"))
    parser.add_argument("--drive", required=True)
    parser.add_argument("--serial", required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[D-Z]:[/\\]?", args.drive, re.I) or not re.fullmatch(r"[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{4}", args.serial):
        parser.error("Use a removable drive and its eight-digit exFAT serial")
    card, serial = Path(args.drive[:2] + "/"), int(args.serial.replace("-", ""), 16)
    source = Path(__file__).resolve().parent
    result = prepare(source, card, serial) if args.action == "PREPARE" else (plan(card, serial) if args.action == "PLAN" else status(card, serial))
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()

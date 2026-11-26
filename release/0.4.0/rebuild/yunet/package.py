"""Package the YuNet direct-entry field build and verify SD readback."""

import argparse
from datetime import datetime
from hashlib import sha256
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile

from build import BUNDLE, BUILD, EYE, FOLDER, HERE, PKG, PREVIOUS_BUILD, WORK, card_pins


sys.path.insert(0, str(HERE.parent))
from coexist_rebuild import copy_new, identity, root_entry, sha


ZIP = WORK / "dist/NX500_0.4.0_EyeYuNet_2_Direct_Field.zip"
TRIGGERS = ("install.sh", "nx_cs.adj", "info.tg")
REQUEST = b"REBUILD\n"
SHELL = "C:/Program Files/Git/bin/sh.exe"
TEST_DOC = HERE / "TEST_zh.md"


def load(path):
    return json.loads(path.read_text("utf-8"))


def validation():
    loader = load(PREVIOUS_BUILD / "loader-validation.json")
    pins = load(BUNDLE / "rebuild-checksums.json")
    if pins != card_pins():
        raise ValueError("Field payload differs from the build manifest")
    digest = pins[f"{FOLDER}/nxl-eye-af.so"]
    fixed, = (row for row in loader if row["sha256"] == digest and row["return"] == 0
              and row["relocation_gap_bytes"] == 0 and row["wrapper_bound"])
    shell = (*sorted(PKG.glob("*.sh")), PKG / "activation/install.sh")
    for source in shell:
        subprocess.run([SHELL, "-n", str(source)], capture_output=True, check=True, timeout=8)
    worker = (PKG / "worker.sh").read_text("ascii")
    if any(token in worker for token in ("mmcblk0p9-before.img", "BACKUP.VERIFIED", 'mv "$RT" "$RT.previous-',
                                         'fail NOTICE_NOT_SHOWN', 'sleep ')):
        raise ValueError("Old backup, directory move or forced wait still in the direct entry")
    return {
        "backend": "yunet32-int8-v1", "payload": pins,
        "scope": "Field build: syntax, manifest, unchanged corrected library; direct handoff timing pending camera test.",
        "shell_syntax_files": len(shell), "firmware_arm_loader_prior": fixed,
    }


def make_zip():
    verified = validation()
    manifest = BUILD / "validation.json"
    manifest.write_text(json.dumps(verified, indent=2) + "\n", encoding="utf-8")
    files = {"unified-rebuild/" + name: BUNDLE / name for name in verified["payload"]}
    files.update({"unified-rebuild/rebuild-checksums.json": BUNDLE / "rebuild-checksums.json",
                  "validation.json": manifest, "TEST_zh.md": TEST_DOC,
                  "YuNet-LICENSE.txt": EYE / "model/YUNET-LICENSE.txt"})
    with ZipFile(ZIP, "x", ZIP_DEFLATED, compresslevel=7) as archive:
        for name, path in files.items():
            archive.write(path, name)
        archive.writestr("unified-rebuild/request", REQUEST)
    with ZipFile(ZIP) as archive:
        if archive.testzip() or set(archive.namelist()) != {*files, "unified-rebuild/request"}:
            raise ValueError("Rebuild ZIP layout differs")
        for path, source in files.items():
            if sha256(archive.read(path)).hexdigest() != sha(source):
                raise ValueError(f"Rebuild ZIP file mismatch: {path}")
    ZIP.with_suffix(".sha256").write_text(f"{sha(ZIP)}  {ZIP.name}\n", encoding="ascii")
    print(ZIP)


def stage(card, serial):
    identity(card, serial)
    existing = sorted(node.name for node in card.iterdir())
    targets = {name: root_entry(card, name) for name in (*TRIGGERS, FOLDER)}
    if any(path.exists() or path.is_symlink() for path in targets.values()):
        raise FileExistsError("The card already has an installer trigger or this rebuild directory")
    def original_lengths_and_times():
        current = set()
        for name in existing:
            if name not in ("System Volume Information", "$RECYCLE.BIN"):
                folder = card / name
                current.add(folder)
                if folder.is_dir() and not folder.is_symlink():
                    current.update(folder.rglob("*"))
        metadata, unreadable = {}, {}
        for path in current:
            try:
                info = path.stat(follow_symlinks=False)
            except OSError as error:
                relative = path.relative_to(card)
                known_logs = relative.parts[:2] == ("NXDEMO51", "logs")
                known_archive = relative.parts[0].startswith("NX500_ARCHIVE_")
                if getattr(error, "winerror", None) != 1392 or not (known_archive or known_logs):
                    raise
                unreadable[relative.as_posix()] = error.winerror
                continue
            metadata[path] = (info.st_size, info.st_mtime_ns)
        return metadata, unreadable

    before, unreadable = original_lengths_and_times()
    with ZipFile(ZIP) as archive, tempfile.TemporaryDirectory(prefix="nx500-yunet-rebuild-") as temporary:
        staged = Path(temporary)
        proof = json.loads(archive.read("validation.json"))
        if proof != validation():
            raise ValueError("Archive changed after verification")
        for full, digest in proof["payload"].items():
            if not re.fullmatch(re.escape(FOLDER) + r"/(?:activation/)?[A-Za-z0-9_.-]+", full):
                raise ValueError(f"Unsafe archive member: {full}")
            blob = archive.read("unified-rebuild/" + full)
            if sha256(blob).hexdigest() != digest:
                raise ValueError(f"Archive digest mismatch: {full}")
            path = staged / full
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(blob)
        required = sum(p.stat().st_size for p in staged.rglob("*") if p.is_file())
        if shutil.disk_usage(card).free <= required + 1048576:
            raise OSError("Insufficient space for the payload and its receipts")
        request = staged / "request"
        if archive.read("unified-rebuild/request") != REQUEST:
            raise ValueError("Wrong camera request")
        request.write_bytes(REQUEST)
        identity(card, serial)
        new = targets[FOLDER]
        new.mkdir()
        (new / "activation").mkdir()
        pending = new / ".entry-stage"
        pending.mkdir()
        new_root = new.resolve()
        for full, digest in proof["payload"].items():
            dest = card / full
            if new_root not in dest.resolve().parents:
                raise ValueError("Rebuild target escaped the selected directory")
            copy_new(staged / full, dest, digest)
        copy_new(request, new / "request", sha(request))
        for name in TRIGGERS:
            full = f"{FOLDER}/activation/{name}"
            copy_new(staged / full, pending / name, proof["payload"][full])
        identity(card, serial)
        published = []
        try:
            for name in TRIGGERS:
                target = targets[name]
                if target.exists() or target.is_symlink():
                    raise FileExistsError(target)
                (pending / name).rename(target)
                published.append(name)
                if sha(target) != proof["payload"][f"{FOLDER}/activation/{name}"]:
                    raise ValueError(f"Root trigger differs: {name}")
            if sorted(node.name for node in card.iterdir()) != sorted(existing + [*TRIGGERS, FOLDER]):
                raise ValueError("Unexpected SD root change during deployment")
            if (before, unreadable) != original_lengths_and_times():
                raise ValueError("Existing project history changed during deployment")
        except BaseException:
            for name in reversed(published):
                target = targets[name]
                if target.is_file() and not target.is_symlink() and sha(target) == proof["payload"][f"{FOLDER}/activation/{name}"]:
                    target.rename(pending / name)
            raise
    archive_errors = {path: error for path, error in sorted(unreadable.items()) if path.startswith("NX500_ARCHIVE_")}
    log_errors = {path: error for path, error in sorted(unreadable.items()) if path.startswith("NXDEMO51/logs/")}
    receipt = {"volume": f"{serial:08X}", "card": str(card), "payload": FOLDER, "activation": "one-shot",
               "readback_files": len(proof["payload"]) + len(TRIGGERS) + 1, "package_sha256": sha(ZIP),
               "existing_root_entries_preserved": len(existing), "existing_paths_metadata_verified": len(before),
               "existing_unreadable_archive_count": len(archive_errors),
               "existing_unreadable_archive_entries": archive_errors,
               "existing_unreadable_log_count": len(log_errors), "existing_unreadable_log_entries": log_errors,
               "staged_at": datetime.now().astimezone().isoformat()}
    path = BUILD / "sd-deployment.json"
    path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in receipt.items() if not key.endswith("_entries")},
                     indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--card", type=Path)
    parser.add_argument("--serial", type=lambda value: int(value.replace("-", ""), 16))
    args = parser.parse_args()
    if args.card:
        if args.serial is None:
            parser.error("--card requires a verified --serial")
        stage(args.card, args.serial)
    else:
        make_zip()

"""Select, inspect or withdraw the existing SD mount callback for full 0.4.0."""

from hashlib import sha256
import json
import os
from pathlib import Path
import re
import shlex
import tempfile
import uuid


ORIGINAL = "9b3154abf884f18b4e3b7009b2a00385b01c9ed8cbab6ead4a517f5b5bb61a18"
PACKAGE = "nxl-full040-simple-wake"
TRIGGERS = ("info.tg", "nx_cs.adj", "install.sh", "nx-lut-demo/eye-af-bt-request")
BODY = Path(__file__).with_name("simple-wake-body.sh")


def only(parent, name, *, directory=False):
    with os.scandir(parent) as stream:
        matches = [e for e in stream if e.name.casefold() == name.casefold()]
    if len(matches) != 1:
        raise ValueError(f"Missing or ambiguous entry: {parent / name}")
    item = matches[0]
    if not (item.is_dir(follow_symlinks=False) if directory else item.is_file(follow_symlinks=False)):
        raise ValueError(f"Unexpected entry type: {item.path}")
    path = Path(item.path)
    if path.resolve().parent != parent.resolve():
        raise ValueError(f"Unexpected resolved path: {path}")
    return path


def small(path, maximum=24576):
    with path.open("rb") as stream:
        data = stream.read(maximum + 1)
    if len(data) > maximum:
        raise ValueError(f"Oversized entry: {path}")
    return data


def render(run, body=None):
    if not re.fullmatch("[0-9a-f]{32}", run):
        raise ValueError("Invalid trial identifier")
    if body is None:
        body = BODY.read_text(encoding="utf8")
    if "@RUN@" not in body:
        raise ValueError("Missing trial identifier")
    # The outer shell closes its SD-backed script on exec. The mount listener
    # can accept later mount events while the fixed, RAM-backed worker waits.
    return (f"#!/bin/sh\n# NX500 full040 SD-mount trial {run}\ncd / || exit 1\nexec /bin/sh -c "
            + shlex.quote(body.replace("@RUN@", run)) + "\n").encode()


def new_file(path, data):
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    if small(path) != data:
        raise OSError(f"Readback failed: {path}")


def change_entry(path, before, after, confirm):
    handle, filename = tempfile.mkstemp(prefix=".nxl-wake-", dir=path.parent)
    temp = Path(filename)
    try:
        with os.fdopen(handle, "wb") as stream:
            stream.write(after)
            stream.flush()
            os.fsync(stream.fileno())
        if small(temp) != after:
            raise OSError("Staged SD callback readback failed")
        confirm()
        if small(only(path.parent, path.name)) != before:
            raise ValueError("The active SD callback changed during staging")
        os.replace(temp, path)
        confirm()
        if small(only(path.parent, path.name)) != after:
            raise OSError("Final SD callback readback failed; preserve the saved original")
    finally:
        if temp.exists():
            confirm()
            if temp.resolve().parent == path.parent.resolve():
                temp.unlink()


def manage(card, action, confirm):
    confirm()
    # These three PC operations only inspect or select files on removable media.
    result = {"action": action, "scope": "sd_callback_only", "camera_install_requested": False}
    directory = only(card, "nx-on-wake", directory=True)
    hook = only(directory, "on-wake")
    active = small(hook)
    baseline = sha256(active).hexdigest() == ORIGINAL
    if action == "SIMPLE-WAKE" and any(os.path.lexists(card/n) for n in TRIGGERS):
        raise ValueError("An existing factory or Bluetooth installation request is present")
    pkg = card / PACKAGE
    if not baseline:
        sig = re.match(rb"\A#!/bin/sh\n# NX500 full040 SD-mount trial ([0-9a-f]{32})\n", active)
        if not sig:
            raise ValueError("Unknown live callback preserved")
        run = sig[1].decode()
        trial = only(only(card, PACKAGE, directory=True), "trial-" + run, directory=True)
        saved = small(only(trial, "original.bin"))
        if active != render(run) or sha256(saved).hexdigest() != ORIGINAL:
            raise ValueError("The active callback or its saved original does not match this release")
        if action == "SIMPLE-RESTORE":
            change_entry(hook, active, saved, confirm)
            return {**result, "trial": run, "sd_callback": "original", "original_restored": True}
        calls = []
        for i in range(1, 4):
            name = f"call-{i}"
            if not os.path.lexists(trial/name):
                continue
            folder = only(trial, name, directory=True)
            record = {"slot":i}
            for leaf in ("entry.txt", "outcome.txt"):
                if os.path.lexists(folder/leaf):
                    record[leaf] = small(only(folder, leaf), 4096).decode("utf-8")
            calls.append(record)
        confirm()
        header = ["entry=sd_mount", "trial=" + run]
        recorded = any(call.get("entry.txt", "").splitlines()[:2] == header for call in calls)
        return {**result, "trial": run, "sd_callback": "trial", "calls": calls,
                "camera_callback_receipt": "entry_recorded" if recorded else "no_matching_receipt"}
    if action != "SIMPLE-WAKE":
        return {**result, "sd_callback": "original"}
    if not pkg.exists():
        pkg.mkdir()
    pkg = only(card, PACKAGE, directory=True)
    run = uuid.uuid4().hex
    trial = pkg / ("trial-"+run)
    trial.mkdir()
    new_file(trial / "original.bin", active)
    source = render(run)
    new_file(trial / "identity.json", (json.dumps({"trial":run, "original_sha256":ORIGINAL,
             "callback_sha256":sha256(source).hexdigest(), "callback_bytes":len(source),
             "entry":"existing_nx_on_wake_sd_mount", "requires_new_image":False},indent=2)+"\n").encode())
    change_entry(hook, active, source, confirm)
    return {**result, "sd_callback": "trial", "trial":run, "callback_sha256":sha256(source).hexdigest(),
            "callback_bytes":len(source), "saved_original_sha256":sha256(small(trial/"original.bin")).hexdigest(),
            "factory_entries_present":[n for n in TRIGGERS if os.path.lexists(card/n)]}

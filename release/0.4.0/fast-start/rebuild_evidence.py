"""Read the existing generation's receipts without staging a camera operation."""

import hashlib
import os
from pathlib import Path
import re


EVENT = "hib_post.service"
WAKE = "nxl-full040-resume.service"
LIBRARY = "/usr/local/lib/nxl-full040/nxl-eye-af.so"


def entry(directory, name, *, folder=False):
    with os.scandir(directory) as entries:
        matches = [item for item in entries if item.name.casefold() == name.casefold()]
    if len(matches) != 1:
        raise ValueError(f"Missing or ambiguous directory entry: {directory / name}")
    item = matches[0]
    valid = item.is_dir(follow_symlinks=False) if folder else item.is_file(follow_symlinks=False)
    if not valid:
        raise ValueError(f"Unexpected entry type: {directory / name}")
    return Path(item.path)


def latest(directory, prefix):
    pattern = re.compile(re.escape(prefix) + r"_(\d+)_(\d+)$", re.I)
    with os.scandir(directory) as entries:
        names = [item.name for item in entries if pattern.fullmatch(item.name)]
    if not names:
        raise ValueError(f"No {prefix} reports in {directory}")
    orders = {name: tuple(map(int, pattern.fullmatch(name).groups())) for name in names}
    order = max(orders.values())
    newest = [name for name in names if orders[name] == order]
    if len(newest) != 1:
        raise ValueError(f"Ambiguous latest {prefix} report")
    return entry(directory, newest[0], folder=True)


def read(directory, name):
    path = entry(directory, name)
    with path.open("rb") as stream:
        data = stream.read(65537)
    if len(data) > 65536:
        raise ValueError(f"Oversized snapshot receipt: {path}")
    return data.decode("utf-8")


def field(text, key):
    values = [row[len(key) + 1:] for row in text.splitlines() if row.startswith(key + "=")]
    if len(values) != 1:
        raise ValueError(f"Missing or repeated receipt field: {key}")
    return values[0]


def snapshot_evidence(card, expected_wake_sha):
    wake = entry(entry(card, "nx-on-wake", folder=True), "on-wake")
    with wake.open("rb") as stream:
        wake_bytes = stream.read(4097)
    if len(wake_bytes) > 4096 or hashlib.sha256(wake_bytes).hexdigest() != expected_wake_sha:
        raise ValueError("The card does not contain the expected full 0.4.0 wake entry")
    records = entry(card, "NXDEMO51", folder=True)
    registration = latest(records, "FULL040_NORMAL_WAKE")
    graph = latest(records, "FULL040_GRAPH_READ")
    reg_result = read(registration, "result.txt")
    graph_result = read(graph, "result.txt")
    if field(reg_result, "exit") != "0" or field(reg_result, "result") != "POST_RESUME_REGISTERED":
        raise ValueError("Latest receipt does not confirm registration")
    if field(graph_result, "exit") != "0":
        raise ValueError("Latest restored-manager observation is incomplete")
    reg_wants = field(read(registration, "post-wants.txt"), "Wants").split()
    parent = read(graph, "parent.txt")
    graph_wants = field(parent, "Wants").split()
    triggers = field(read(graph, "path.txt"), "Triggers").split()
    runtime = read(graph, "persisted-and-runtime.txt")
    boot = field(runtime, "boot_id")
    if not re.fullmatch(r"[\da-f]{8}(?:-[\da-f]{4}){3}-[\da-f]{12}", boot):
        raise ValueError("Invalid restored generation identifier")
    if field(runtime, "installed_files_and_link") != "1" or field(runtime, "hib_post") != "1":
        raise ValueError("The file identities or restored boot phase were not confirmed")
    if (EVENT not in triggers or WAKE not in reg_wants or
            field(parent, "Result") != "success" or field(parent, "ExecMainStatus") != "0"):
        raise ValueError("Registration and stock event receipts disagree")
    listed = any(WAKE in row.split() for row in read(graph, "initial-units.txt").splitlines())
    graph_verdict = field(graph_result, "result")
    if graph_verdict != ("RESTORED_MANAGER_HAS_DEPENDENCY" if WAKE in graph_wants
                         else "DISK_REGISTERED_BUT_RESTORED_EVENT_LACKS_DEPENDENCY"):
        raise ValueError("The graph verdict and raw Wants property disagree")

    # Match the graph to an actual restored camera claim. An inspection run may
    # have a newer directory name than the capture it was inspecting.
    claims = set()
    for report in records.iterdir():
        if not re.fullmatch(r"FAST_START_EYE_AF_ROOTLIB_\d+_\d+", report.name):
            continue
        claim = report / "launch-claim"
        if not claim.is_file():
            continue
        text = read(entry(records, report.name, folder=True), "launch-claim")
        if field(text, "boot_id") == boot:
            claims.add((field(text, "policy"), field(text, "file"), field(text, "camera_pid"),
                        field(text, "mapped_dev"), field(text, "mapped_ino")))
    if len(claims) != 1:
        raise ValueError("No unique restored camera claim matches this graph")
    policy, library, pid, device, inode = claims.pop()
    if (policy != "EYE" or library != LIBRARY or not re.fullmatch(r"[1-9]\d*", pid)
            or not re.fullmatch(r"[0-9a-fA-F]+:[0-9a-fA-F]+", device)
            or not re.fullmatch(r"[1-9]\d*", inode)):
        raise ValueError("The restored claim is not for the full 0.4.0 eye module")
    if WAKE not in graph_wants:
        hold = "RESTORED_EVENT_MISSING_COMPANION"
    else:
        hold = "RESTORED_BOOT_IS_NOT_A_PREWRITE_ATTESTATION"
    return {
        "existing_full040_generation": boot,
        "registration_report": registration.name,
        "restored_graph_report": graph.name,
        "stock_event": EVENT,
        "registered_wants": reg_wants,
        "restored_wants": graph_wants,
        "companion_initially_listed": listed,
        "install_entries_present": [n for n in ("info.tg", "nx_cs.adj", "install.sh", "nx-lut-demo/eye-af-bt-request")
                                    if os.path.lexists(card / n)],
        "phase": "previous_restored_boot",
        "write_permitted": False,
        "hold": hold,
    }

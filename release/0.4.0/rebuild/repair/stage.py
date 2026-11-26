"""Prepare one verified fast-start profile on a selected Windows SD card."""

import argparse
import json
import os
from pathlib import Path
import re
import tempfile

from card_files import copy_new, identity, root_entry, sha


TRIGGERS = ("install.sh", "nx_cs.adj", "info.tg")
PROFILES = {"EYE28": "nxl-full040-r11-eye28", "YUNET": "nxl-full040-r11-yunet"}
INTERFACE = "full040-repair-1"


def available(path):
    return not path.exists() and not path.is_symlink()


def verify_pkg(source):
    pins = json.loads((source / "checksums.json").read_text("utf-8"))
    required = {"fileops.arm", "worker.sh", "launch-select.sh", "wake-select.sh", "verify-before-capture.sh",
                "nxl-eye-af.so", *("activation/" + f for f in TRIGGERS)}
    if not required <= pins.keys():
        raise ValueError("安装包缺少必要文件")
    for name, expected in pins.items():
        path = source / name
        if not re.fullmatch(r"(?:activation/)?[A-Za-z0-9_-][A-Za-z0-9_.-]*", name) or not path.is_file() or path.is_symlink():
            raise ValueError(f"安装包文件路径异常 {name}")
        if not re.fullmatch(r"[0-9a-f]{64}", expected) or sha(path) != expected:
            raise ValueError(f"安装包文件校验失败 {name}")
    return pins


def prepare(source, card, serial, request):
    token = {"CHECK": b"CHECK\n", "REBUILD": b"REBUILD\n"}[request]
    pins = verify_pkg(source)
    identity(card, serial)
    target = root_entry(card, source.name)
    for name in (*TRIGGERS, "nx500.bin"):
        if not available(root_entry(card, name)):
            raise FileExistsError(f"已有安装触发文件 {name}，本次未写卡")
    for folder, leaf in (("nx-on-wake", "on-wake"), ("install", "install.sh")):
        directory = root_entry(card, folder)
        if not available(directory / leaf):
            raise FileExistsError(f"已有独立启动入口 {folder}/{leaf}，本次未写卡")
    if not available(target):
        if not target.is_dir() or target.is_symlink() or not (target / "activation").is_dir() or (target / "activation").is_symlink():
            raise ValueError("卡上已有同名目录，无法核对归属")
        if any((target / name).is_symlink() or sha(target / name) != expected for name, expected in pins.items()):
            raise ValueError("卡上已有同名载荷且内容不同")
    else:
        pending = Path(tempfile.mkdtemp(prefix=source.name + "-stage-", dir=card))
        (pending / "activation").mkdir()
        for name, expected in pins.items():
            copy_new(source / name, pending / name, expected)
        identity(card, serial)
        pending.rename(target)
    prior = target / "request"
    if not available(prior) and (prior.is_symlink() or not prior.is_file() or prior.read_bytes() not in (b"CHECK\n", b"REBUILD\n")):
        raise ValueError("旧请求无法核对归属")
    pending = Path(tempfile.mkdtemp(prefix="entry-", dir=target))
    request_file = pending / "request"
    with request_file.open("xb") as stream:
        stream.write(token)
        stream.flush()
        os.fsync(stream.fileno())
    for name in TRIGGERS:
        copy_new(source / "activation" / name, pending / name, pins["activation/" + name])
    if prior.is_file():
        copy_new(prior, pending / "previous-request", sha(prior))
    identity(card, serial)
    request_file.replace(prior)
    if prior.read_bytes() != token:
        raise OSError("请求写入后读回失败")
    # No active install entry is published until the full payload and request are verified.
    published = []
    try:
        for name in TRIGGERS:
            identity(card, serial)
            dest = root_entry(card, name)
            if not available(dest):
                raise FileExistsError(dest)
            (pending / name).rename(dest)
            published.append(name)
            if sha(dest) != pins["activation/" + name]:
                raise OSError(f"安装入口读回失败 {name}")
    except BaseException:
        identity(card, serial)
        for name in reversed(published):
            path = root_entry(card, name)
            if path.is_file() and not path.is_symlink() and sha(path) == pins["activation/" + name]:
                path.rename(pending / name)
        raise
    return {"result": "SD_READY", "camera_capture_completed": False, "profile": source.name,
            "request": request, "files_verified": len(pins) + len(TRIGGERS) + 1}


def status(card, serial, generation):
    identity(card, serial)
    reports = root_entry(card, "NXDEMO51")
    for report in sorted(reports.glob("FAST_START_EYE_AF_ROOTLIB_*_*"), reverse=True):
        if report.is_symlink() or not re.fullmatch(r"FAST_START_EYE_AF_ROOTLIB_\d+_\d+", report.name):
            continue
        prepare_log = report / "prepare.log"
        if prepare_log.is_symlink() or not prepare_log.is_file():
            continue
        with prepare_log.open(encoding="utf-8", errors="replace") as stream:
            lines = stream.read(4096).splitlines()
        if f"interface={INTERFACE}" not in lines or f"eye_build={generation}" not in lines:
            continue
        result = {"report": report.name, "profile": generation,
                  "triggers_present": [f for f in TRIGGERS if not available(root_entry(card, f))]}
        for name in ("result.txt", "CAPTURE_REQUESTED", "resume/normal-start.txt", "resume/full-menu.txt"):
            path = report / name
            if path.is_file() and not path.is_symlink() and not path.parent.is_symlink():
                result[name] = path.read_text("utf-8", errors="replace")
        return result
    return {"result": "NO_CAMERA_RECEIPT", "profile": generation}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("CHECK", "REBUILD", "STATUS"))
    parser.add_argument("profile", choices=tuple(PROFILES))
    parser.add_argument("--drive", required=True)
    parser.add_argument("--serial", required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[D-Z]:[/\\]?", args.drive, re.I) or not re.fullmatch(r"[0-9a-f]{4}-?[0-9a-f]{4}", args.serial, re.I):
        parser.error("指定 SD 卡盘符和 8 位 exFAT 卷序列号")
    card = Path(args.drive[:2] + "/")
    serial = int(args.serial.replace("-", ""), 16)
    generation = PROFILES[args.profile]
    if args.action == "STATUS":
        result = status(card, serial, generation)
    else:
        result = prepare(Path(__file__).resolve().parent / "profiles" / generation, card, serial, args.action)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()

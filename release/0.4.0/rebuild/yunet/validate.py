"""Validate the actual YuNet rebuild payload without using camera hardware."""

import argparse
import json
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile

from build import BUNDLE, BUILD, HERE, PKG, RELEASE_ZIP, WORK, card_pins, sha


sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(WORK.parent / "nx500-040-release/snapshot-graph/native-test"))
import test_rebuild as fixture


def save(name, result):
    (BUILD / name).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


def validate_abi():
    from elftools.elf.elffile import ELFFile

    with (PKG / "nxl-eye-af.so").open("rb") as stream:
        elf = ELFFile(stream)
        assert elf["e_machine"] == "EM_ARM" and elf["e_flags"] == 0x05000200 and elf.elfclass == 32
        symbols = elf.get_section_by_name(".dynsym").iter_symbols()
        exports = {sym.name for sym in symbols if sym["st_shndx"] != "SHN_UNDEF" and
                   sym["st_info"]["bind"] in ("STB_GLOBAL", "STB_WEAK") and sym.name}
        assert exports == {"_ZN20CFaceDetection_Linux13FaceDetectionEPhiiii"}
        dynamic = elf.get_section_by_name(".dynamic")
        soname = next(tag.soname for tag in dynamic.iter_tags("DT_SONAME"))
        assert soname == "nxl-eye-af.so"
        tags = {tag.entry.d_tag: tag.entry.d_val for tag in dynamic.iter_tags()}
        rel_end = tags["DT_REL"] + tags["DT_RELSZ"]
        unwind = elf.get_section_by_name(".ARM.exidx")
        assert rel_end == tags["DT_JMPREL"] and unwind["sh_addr"] + unwind["sh_size"] <= tags["DT_REL"]
        versions = {aux.name for _, entries in elf.get_section_by_name(".gnu.version_r").iter_versions()
                    for aux in entries}
        assert versions == {"GLIBC_2.4"}
    assert b"SDIC_EyeDetection" not in (PKG / "nxl-eye-af.so").read_bytes()
    return {"soname": soname, "exported": sorted(exports), "symbols_require": sorted(versions),
            "relocation_gap_bytes": tags["DT_JMPREL"] - rel_end,
            "stock_eye_link": False, "library_sha256": sha(PKG / "nxl-eye-af.so")}


def collector_test(mode):
    with tempfile.TemporaryDirectory(prefix="nx-unified-collector-") as tmp:
        root = Path(tmp)
        fixture.fixtures(root)
        leaf = "FAST_START_EYE_AF_ROOTLIB_100_300"
        result = root / "mnt/mmc/NXDEMO51" / leaf / "resume"
        result.mkdir(parents=True)
        (root / "proc/500").mkdir()
        status = root / "tmp/nx-lut-lite/eye-af.status"
        status_line = "NXL_EYE_AF2 pid=500 state=ACTIVE extra=2 side=1 model=yunet32-int8-v1 schema=28"
        fixture.write(status, status_line.replace("pid=500", "pid=820") + "\n")
        fixture.write(result / "normal-start.txt", "retained\n")
        script = (PKG / "collect-resume.sh").read_text("ascii").replace("task_root=\n", """task_root=.
iterations=0
sleep() {
    iterations=$((iterations+1))
    if [ "$iterations" = 1 ]; then
        printf '%s\n' 'NXL_EYE_AF2 pid=500 state=ACTIVE extra=2 side=1 model=yunet32-int8-v1 schema=28' > ./tmp/nx-lut-lite/eye-af.status
    elif [ "$iterations" = 2 ]; then
        printf '%s\n' 'NXL_EYE_AF2 pid=500 state=FACE_ONLY extra=3 side=0 model=yunet32-int8-v1 schema=28' > ./tmp/nx-lut-lite/eye-af.status
    fi
}
""")
        fixture.write(root / "collect.sh", script)
        if mode == "usb":
            fixture.write(root / "sys/class/extcon/USB/state", "1\n")
        result_run = subprocess.run([fixture.SH, "-c", '/bin/sh ./collect.sh "$@"', "fixture", "500", leaf],
                                    cwd=root, capture_output=True, timeout=35)
        outputs = list(result.glob("yunet-af-*/samples.tsv"))
        assert (result / "normal-start.txt").read_text() == "retained\n"
        if mode == "usb":
            assert result_run.returncode != 0 and not outputs and not (result / "yunet-af.complete").exists()
            return {"report_written": False}
        assert result_run.returncode == 0, result_run.stderr
        samples, = outputs
        assert len(samples.read_text().splitlines()) == 3
        assert "pid=820" not in samples.read_text() and "state=FACE_ONLY" in samples.read_text()
        assert (samples.parent / "result.txt").read_text() == "result=DONE\nrows=2\n"
        assert "activation=absent" in (samples.parent / "start.txt").read_text()
        repeat = subprocess.run([fixture.SH, "-c", '/bin/sh ./collect.sh "$@"', "fixture", "500", leaf],
                                cwd=root, capture_output=True, timeout=15)
        assert repeat.returncode == 0 and list(result.glob("yunet-af-*/samples.tsv")) == outputs
        return {"report_written": True, "status_rows": 2, "foreign_pid_ignored": True, "repeat_skipped": True}


def validate_worker():
    report = {"abi": validate_abi(), "worker": {}, "wake": {}, "collector": {}}
    for file in (*PKG.glob("*.sh"), PKG / "activation/install.sh"):
        subprocess.run([fixture.SH, "-n", str(file)], capture_output=True, check=True)
    mnp = WORK / "mnp-final-tree/nxl-full040-mnp1"
    old_yunet = WORK / "yunet-rebuild1/unified-rebuild/nxl-full040-yunet1"
    for case in ("yunet1-upgrade", "yunet1-reject-graph", "yunet1-mixed-runtime", "bad-payload"):
        report["worker"][case] = fixture.worker_test(BUNDLE, RELEASE_ZIP, case,
                                                     mnp_runtime=mnp, yunet_runtime=old_yunet)
        print("PASS worker", case, flush=True)
    for case in ("success", "no-sd", "wrong-map"):
        report["wake"][case] = fixture.wake_test(BUNDLE, case)
        print("PASS wake", case, flush=True)
    for case in ("samples", "usb"):
        report["collector"][case] = collector_test(case)
        print("PASS collector", case, flush=True)
    report["payload"] = card_pins()
    report["scope"] = "Production shell and ARM ELF layout; mocked worker filesystem and camera endpoints."
    save("worker-validation.json", report)


def validate_manager():
    from native_replay import native, profile

    cache_source = WORK / "mnp-native-qemu"
    out = BUILD / "manager-validation.json"
    tar = BUILD / "manager-profile.tar"
    tar.write_bytes(profile(BUNDLE))
    old_guest = native.guest

    def guest(data, binary, case):
        disk = old_guest(data, binary, case)
        if case == "capture-old-master":
            disk.add("etc/systemd/system/multi-user.target.wants/nx-on-wake.service",
                     stat.S_IFLNK | 0o777, b"/etc/systemd/system/nx-on-wake.service")
        if case == "capture-library-changed":
            name = "usr/local/lib/nxl-full040/nxl-eye-af.so"
            mode, blob = disk.entries[name]
            disk.entries[name] = (mode, blob + b"changed")
        return disk

    native.guest = guest
    with tempfile.TemporaryDirectory(prefix="nx500-yunet-manager-") as directory:
        cache = Path(directory)
        shutil.copyfile(cache_source / "vmlinuz", cache / "vmlinuz")
        shutil.copytree(cache_source / "qemu-bin", cache / "qemu-bin")
        cases = ("combined", "capture-combined", "capture-stale-manager", "capture-old-master", "capture-library-changed")
        native.run(tar, cache, out, cases)
    report = json.loads(out.read_text("utf-8"))
    assert report["cases"]["capture-library-changed"]["reasons"] == ["UNIFIED_PAYLOAD_MISMATCH"]
    assert report["cases"]["capture-old-master"]["reasons"] == ["OLD_WAKE_MASTER_STILL_ENABLED"]
    report["payload"] = card_pins()
    save("manager-validation.json", report)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("worker", "manager"))
    args = parser.parse_args()
    (validate_worker if args.mode == "worker" else validate_manager)()

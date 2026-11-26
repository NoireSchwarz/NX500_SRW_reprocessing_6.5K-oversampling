"""Verify the Focus update using the same ARM and boot fixtures as Primary v2."""

import argparse
import shutil
import tempfile
from pathlib import Path
from unittest.mock import patch

from build import HERE, RELEASE_ZIP, WORK, sha
from build_focus_upgrade import ARTIFACT, BASE, BUILD, BUNDLE, FOLDER, PKG, PREVIOUS, RUNTIME, card_pins
from package import load
from validate import fixture, save
import validate


validate.BUILD = BUILD


def host():
    old = BASE / PREVIOUS
    legacy = WORK / "yunet-resume-direct/unified-rebuild/nxl-full040-yunet2-direct"
    result = {"payload": card_pins(), "worker": {}, "wake": {}}
    for case in ("primary-upgrade", "primary-split-active", "primary-mixed-runtime", "primary-reject-graph"):
        result["worker"][case] = fixture.worker_test(BUNDLE, RELEASE_ZIP, case, primary_runtime=legacy, predecessor=old)
        print("PASS", case, flush=True)
    for case in ("success", "no-sd", "wrong-generation", "wrong-map", "reject-menu"):
        result["wake"][case] = fixture.wake_test(BUNDLE, case, runtime=FOLDER)
        print("PASS wake", case, flush=True)
    result["selectors"] = selectors()
    save("host-validation.json", result)


def selectors():
    with tempfile.TemporaryDirectory(prefix="nx500-focus-selector-") as scratch:
        root = Path(scratch)
        fixture.fixtures(root)
        fixture.write(root / "run/boot", fixture.CAPTURE + "\n")
        report = "FAST_START_EYE_AF_ROOTLIB_100_300"
        claim = f"boot_id={fixture.CAPTURE}\nfile=./usr/local/lib/{FOLDER}/nxl-eye-af.so\n"
        for action in ("launch", "wake"):
            source = (PKG / f"{action}-select.sh").read_text("ascii")
            source = source.replace("rt=/usr/local", "rt=./usr/local").replace("claim=/opt/usr", "claim=./opt/usr")
            source = source.replace("/proc/sys/kernel/random/boot_id", "./run/boot").replace("/run/nxl-full040", "./run/nxl-full040")
            fixture.write(root / action, source)
            candidate = root / "usr/local/lib" / FOLDER
            fixture.write(candidate / (action + ".sh"), "echo candidate\n")
            fixture.write(candidate / ("legacy-" + action + ".sh"), "echo previous\n")
        fixture.write(candidate / "capture-origin", f"{fixture.BOOT}\nEYE\n{report}\n")
        proof = root / "run/nxl-full040-capture/image-header.1.txt"
        claim_path = root / "opt/usr" / FOLDER / "launch-claim"

        def selected(action):
            run = fixture.run(root, action)
            assert run.returncode == 0, run.stderr
            return run.stdout.decode().strip()

        with patch.dict("os.environ", {"HIB": "1"}):
            assert selected("launch") == selected("wake") == "previous"
            fixture.write(proof, f"boot_id={fixture.CAPTURE}\ncapture_report={report}\neye_policy=primary-v2\ndecision=PASS\n")
            assert selected("launch") == "previous"
            fixture.write(claim_path, claim.replace(f"/{FOLDER}/", f"/{PREVIOUS}/"))
            assert selected("wake") == "previous"
            fixture.write(proof, proof.read_bytes() + b"eye_focus=stable2\n")
            fixture.write(claim_path, claim)
            assert selected("launch") == selected("wake") == "candidate"
            fixture.write(root / "run/boot", fixture.BOOT + "\n")
            assert selected("launch") == selected("wake") == "previous"
    return {"old_boot_and_proof_rejected": True, "new_proof_and_claim_selected": True}


def manager():
    from native_replay import native, profile

    image = BUILD / "manager-profile.tar"
    report = BUILD / "manager-validation.json"
    image.write_bytes(profile(BUNDLE, isolated_runtime=RUNTIME))
    guest_original = native.guest

    def guest(data, binary, case):
        disk = guest_original(data, binary, case)
        if case == "capture-candidate-changed":
            name = f"usr/local/lib/{FOLDER}/nxl-eye-af.so"
            mode, payload = disk.entries[name]
            disk.entries[name] = (mode, payload + b"changed")
        return disk

    native.guest = guest
    source = WORK / "mnp-native-qemu"
    with tempfile.TemporaryDirectory(prefix="nx500-focus-manager-") as scratch:
        cache = Path(scratch)
        shutil.copyfile(source / "vmlinuz", cache / "vmlinuz")
        shutil.copytree(source / "qemu-bin", cache / "qemu-bin")
        native.run(image, cache, report, ("capture-combined", "capture-stale-manager", "capture-candidate-changed"))
    result = load(report)
    assert result["cases"]["capture-candidate-changed"]["reasons"] == ["UNIFIED_PAYLOAD_MISMATCH"]
    result["payload"] = card_pins()
    save("manager-validation.json", result)


def final():
    host_report = load(BUILD / "host-validation.json")
    manager_report = load(BUILD / "manager-validation.json")
    arm_report = load(ARTIFACT / "arm-validation.json")
    pins = card_pins()
    assert pins == host_report.pop("payload") == manager_report.pop("payload")
    assert arm_report["library_sha256"] == sha(PKG / "nxl-eye-af.so")
    save("focus-validation.json", {"payload": pins, "host": host_report, "manager": manager_report,
                                   "arm": arm_report})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("host", "manager", "final"))
    {"host": host, "manager": manager, "final": final}[parser.parse_args().mode]()

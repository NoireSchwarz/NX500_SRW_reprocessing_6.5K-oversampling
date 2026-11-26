"""Build the focus geometry update for the installed YuNet Primary v2 generation."""

import argparse
import json
from pathlib import Path
import re

import package
from build import WORK, replace_one, sha
from build_primary_upgrade import BUNDLE as BASE, FOLDER as PREVIOUS


ARTIFACT = WORK / "yunet-focus-stable2/screen-aligned/final"
BUILD = ARTIFACT.parent / "deploy"
BUNDLE = BUILD / "unified-rebuild"
FOLDER = "nxl-full040-yunet-focus2"
PKG = BUNDLE / FOLDER
ZIP = WORK / "dist/NX500_0.4.0_EyeYuNet_Focus2.zip"
PREVIOUS_MANIFEST = "eda65fb1b2d2f76303ee1d80d601f95df81e0d6ae0f502647f00376941dec5ed"
RUNTIME = ("launch.sh", "dispatch.sh", "full-menu.sh", "wake.sh", "collect-resume.sh",
           "fileops.arm", "legacy-launch.sh", "legacy-wake.sh", "nxl-eye-af.so")


def card_pins():
    return {p.relative_to(BUNDLE).as_posix(): sha(p) for p in sorted(PKG.rglob("*")) if p.is_file()}


def write(name, data):
    path = PKG / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode("ascii") if isinstance(data, str) else data)


def replace_worker_pin(body, path, new):
    pattern = rf'(?m)^pin "{re.escape(path)}" [0-9a-f]{{64}}( \|\| fail \w+)$'
    pin, = re.findall(pattern, body)
    return re.sub(pattern, lambda _: new + pin, body)


def prepare():
    manifest = BASE / "rebuild-checksums.json"
    if sha(manifest) != PREVIOUS_MANIFEST:
        raise ValueError("Primary v2 manifest changed")
    old = {path.removeprefix(PREVIOUS + "/"): digest for path, digest in package.load(manifest).items()}
    files = {name: (BASE / PREVIOUS / name).read_bytes() for name in old}
    if any(sha(BASE / PREVIOUS / name) != digest for name, digest in old.items()):
        raise ValueError("Primary v2 payload changed")
    library = ARTIFACT / "nxl-eye-af-yunet.so"
    loaded, = package.load(ARTIFACT / "loader/loader-validation.json")
    if (loaded["sha256"] != sha(library) or loaded["return"] != 0 or not loaded["wrapper_bound"]
            or loaded["relocation_gap_bytes"] != 0):
        raise ValueError("The candidate must pass the original ARM loader")
    candidate = library.read_bytes()
    if b"focus=stable2" not in candidate or b"SDIC_EyeDetection" in candidate:
        raise ValueError("Wrong focus library")

    PKG.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        write(name, data)
    write("nxl-eye-af.so", candidate)

    def isolated(name):
        return files[name].decode("ascii").replace(PREVIOUS, FOLDER)

    for name in ("dispatch.sh", "full-menu.sh", "launch.sh", "wake-select.sh"):
        write(name, isolated(name))
    for action in ("launch", "wake"):
        write(f"legacy-{action}.sh", files[f"{action}-select.sh"])
    write("launch-select.sh", replace_one(isolated("launch-select.sh"),
          "grep -Fqx 'eye_policy=primary-v2' \"$proof\" &&",
          "grep -Fqx 'eye_focus=stable2' \"$proof\" &&"))
    write("collect-resume.sh", replace_one(isolated("collect-resume.sh"),
          '" policy=primary-v2 model=yunet32-int8-v1',
          '" focus=stable2 "*" policy=primary-v2 model=yunet32-int8-v1'))

    wake = isolated("wake.sh")
    for name in ("nxl-eye-af.so", "full-menu.sh", "collect-resume.sh"):
        wake = replace_one(wake, old[name], sha(PKG / name))
    write("wake.sh", wake)

    checker = isolated("verify-before-capture.sh")
    for name in ("launch-select.sh", "wake-select.sh", "full-menu.sh", "nxl-eye-af.so", "launch.sh", "wake.sh",
                 "legacy-launch.sh", "legacy-wake.sh"):
        checker = replace_one(checker, old[name], sha(PKG / name))
    checker = replace_one(checker, "eye_policy=primary-v2\\n", "eye_policy=primary-v2\\neye_focus=stable2\\n")
    write("verify-before-capture.sh", checker)

    worker = isolated("worker.sh")
    worker = replace_one(worker, "OLD_STATE=$task_root/opt/usr/nxl-full040\n",
                         f"OLD_STATE=$task_root/opt/usr/{PREVIOUS}\n")
    worker = replace_one(worker, "OLD_RT=$task_root/usr/local/lib/nxl-full040\n",
                         f"OLD_RT=$task_root/usr/local/lib/{PREVIOUS}\nCANON=$task_root/usr/local/lib/nxl-full040\n")
    for name in ("launch.sh", "dispatch.sh", "full-menu.sh", "nxl-eye-af.so", "wake.sh", "collect-resume.sh"):
        worker = replace_worker_pin(worker, f"$OLD_RT/{name}", f'pin "$OLD_RT/{name}" {old[name]}')
    for name in ("readout", "verify-before-capture.sh"):
        worker = replace_worker_pin(worker, f"$OLD_RT/{name}", f'pin "$CANON/{name}" {old[name]}')
    anchor = 'pin "$CANON/verify-before-capture.sh"'
    canon = "".join(f'pin "$CANON/{action}.sh" {old[action + "-select.sh"]} || fail INSTALLED_GENERATION_MISMATCH\n'
                    for action in ("launch", "wake"))
    worker = replace_one(worker, anchor, canon + anchor)
    for name in (*RUNTIME, "launch-select.sh", "wake-select.sh", "verify-before-capture.sh"):
        worker = replace_worker_pin(worker, f"$PKG/{name}", f'pin "$PKG/{name}" {sha(PKG / name)}')
    worker = replace_one(worker, "previous_runtime=eye-yunet2-direct\\neye_policy=primary-v2\\n",
                         "previous_runtime=eye-yunet-primary2\\neye_policy=primary-v2\\neye_focus=stable2\\n")
    write("worker.sh", worker)
    write("activation/install.sh", replace_one(isolated("activation/install.sh"), old["worker.sh"], sha(PKG / "worker.sh")))
    (BUNDLE / "rebuild-checksums.json").write_text(json.dumps(card_pins(), indent=2) + "\n", encoding="utf-8")


def configure_package():
    package.BUILD, package.BUNDLE, package.PKG, package.FOLDER = BUILD, BUNDLE, PKG, FOLDER
    package.PREVIOUS_BUILD, package.ZIP, package.card_pins = ARTIFACT / "loader", ZIP, card_pins
    package.TEST_DOC = Path(__file__).with_name("TEST_Focus_zh.md")


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("mode", choices=("prepare", "package", "stage"))
    cli.add_argument("--card", type=Path)
    cli.add_argument("--serial", type=lambda value: int(value.replace("-", ""), 16))
    args = cli.parse_args()
    if args.mode == "prepare":
        prepare()
        return
    configure_package()
    verified = package.validation()
    proof = package.load(BUILD / "focus-validation.json")
    if proof["payload"] != verified["payload"]:
        raise ValueError("Tested payload differs from the deployment payload")
    verified["scope"] = "ARM AF, LCD, loader and service manager replays, with simulated camera and storage endpoints."
    verified["focus_upgrade"] = {key: value for key, value in proof.items() if key != "payload"}
    package.validation = lambda: verified
    if args.mode == "package":
        package.make_zip()
    elif args.card is None or args.serial is None:
        cli.error("stage requires the verified card and volume serial")
    else:
        package.stage(args.card, args.serial)


if __name__ == "__main__":
    main()

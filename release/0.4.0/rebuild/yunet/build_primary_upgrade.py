"""Prepare the Primary v2 update from the field-confirmed YuNet 2 installation."""

import argparse
import json
from pathlib import Path

import package
from build import FOLDER as BASE_FOLDER, WORK, replace_one, sha

BASE = WORK / "yunet-resume-direct/unified-rebuild"
ARTIFACT = WORK / "yunet-primary-v2/final"
BUILD = WORK / "yunet-primary-v2/deploy"
BUNDLE = BUILD / "unified-rebuild"
FOLDER = "nxl-full040-yunet-primary2"
PKG = BUNDLE / FOLDER
ZIP = WORK / "dist/NX500_0.4.0_EyeYuNet_Primary2.zip"
RT_NAME = "nxl-full040"
RT_PATH = "/usr/local/lib/" + RT_NAME
STATE_PATH = "/opt/usr/" + RT_NAME
NEW_RT = "/usr/local/lib/" + FOLDER
NEW_STATE = "/opt/usr/" + FOLDER
BASE_MANIFEST = "d1df359e4a92006bfcbc50e5a745871ddbd861188b81b227c62606c569219607"
LAUNCH_SELECT = '''#!/bin/sh
# Use the candidate only in a newly prepared capture boot.
rt=@NEW_RT@
if [ -n "$HIB" ] && [ -f "$rt/capture-origin" ] && [ ! -L "$rt/capture-origin" ]; then
    boot=$(cat /proc/sys/kernel/random/boot_id)
    report=$(sed -n '3p' "$rt/capture-origin")
    for proof in /run/nxl-full040-capture/image-header.*.txt; do
        if [ -n "$boot" ] && [ -n "$report" ] && [ -f "$proof" ] && [ ! -L "$proof" ] &&
            grep -Fqx "boot_id=$boot" "$proof" && grep -Fqx "capture_report=$report" "$proof" &&
            grep -Fqx 'eye_policy=primary-v2' "$proof" && grep -Fqx 'decision=PASS' "$proof"; then
            exec /bin/sh "$rt/launch.sh" "$@"
        fi
    done
fi
exec /bin/sh "$rt/legacy-launch.sh" "$@"
'''
WAKE_SELECT = '''#!/bin/sh
# The restored camera's claim selects the wake path. A staged file is insufficient.
rt=@NEW_RT@
claim=@NEW_STATE@/launch-claim
boot=$(cat /proc/sys/kernel/random/boot_id)
if [ -n "$boot" ] && [ -f "$claim" ] && [ ! -L "$claim" ] &&
    grep -Fqx "boot_id=$boot" "$claim" && grep -Fqx "file=$rt/nxl-eye-af.so" "$claim"; then
    exec /bin/sh "$rt/wake.sh" "$@"
fi
exec /bin/sh "$rt/legacy-wake.sh" "$@"
'''


def card_pins():
    return {p.relative_to(BUNDLE).as_posix(): sha(p) for p in sorted(PKG.rglob("*")) if p.is_file()}


def isolate(body):
    return body.replace(RT_PATH + "/", "/usr/local/lib/" + FOLDER + "/").replace(
        STATE_PATH + "\n", "/opt/usr/" + FOLDER + "\n"
    ).replace(RT_PATH + "\n", "/usr/local/lib/" + FOLDER + "\n")


def write(relative, body):
    destination = PKG / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(body.encode("ascii") if isinstance(body, str) else body)


def update_capture_checker(old, checker):
    for name in ("launch.sh", "wake.sh"):
        checker = replace_one(checker, old[name], sha(PKG / name.replace(".sh", "-select.sh")))
    for name in ("nxl-eye-af.so", "full-menu.sh"):
        checker = replace_one(checker, old[name], sha(PKG / name))
        checker = checker.replace(RT_PATH + "/" + name, NEW_RT + "/" + name)
    entry = []
    for name in ("launch.sh", "wake.sh", "fileops.arm", "legacy-launch.sh", "legacy-wake.sh"):
        entry.append(f'path=$root{NEW_RT}/{name}\n'
                     f'[ -f "$path" ] && [ ! -L "$path" ] && [ "$("$ops" sha256 "$path")" = {sha(PKG / name)} ] || '
                     'stop CANDIDATE_PAYLOAD_MISMATCH\n')
    checker = replace_one(checker, f"origin=$root{RT_PATH}/capture-origin",
                          "".join(entry) + f"origin=$root{NEW_RT}/capture-origin")
    checker = replace_one(checker, "capture_report=%s\\n", "capture_report=%s\\neye_policy=primary-v2\\n")
    return checker


def update_worker(original, old):
    worker = replace_one(original, "PKG=$SD/" + BASE_FOLDER, "PKG=$SD/" + FOLDER)
    worker = replace_one(worker, "STATE=$task_root" + STATE_PATH + "\nRT=$task_root" + RT_PATH,
                         f"OLD_STATE=$task_root{STATE_PATH}\nOLD_RT=$task_root{RT_PATH}\n"
                         f"STATE=$task_root/opt/usr/{FOLDER}\nRT=$task_root/usr/local/lib/{FOLDER}")
    worker = replace_one(worker, "state_saved=0; ", "")
    worker = replace_one(worker, "; link_added=0; master_detached=0", "")
    stop = worker.index('    if [ "$handed" = 0 ]; then')
    cleanup = worker.index('        if [ "$link_added" = 1 ]', stop)
    finish = worker.index("    fi\n    root_back ||", cleanup)
    worker = worker[:cleanup] + '''        if [ "$rt_new" = 1 ]; then mv "$RT" "$RT.failed-$tag" || printf 'ROLLBACK_RUNTIME_SAVE_FAILED\\n'; fi
        if [ "$state_new" = 1 ]; then mv "$STATE" "$STATE.failed-$tag" || printf 'ROLLBACK_STATE_SAVE_FAILED\\n'; fi
''' + worker[finish:]
    for name in ("collect-resume.sh", "dispatch.sh", "full-menu.sh", "launch.sh", "nxl-eye-af.so",
                 "nxl-full040-resume.service", "verify-before-capture.sh", "wake.sh"):
        anchor = f'pin "$PKG/{name}" {old[name]} || fail PAYLOAD_IDENTITY'
        worker = replace_one(worker, anchor, anchor.replace(old[name], sha(PKG / name)))
    anchor = 'pin "$PKG/capture-profile.conf"'
    selector_pins = "".join(f'pin "$PKG/{name}" {sha(PKG / name)} || fail PAYLOAD_IDENTITY\n'
                           for name in ("launch-select.sh", "wake-select.sh", "legacy-launch.sh", "legacy-wake.sh"))
    worker = replace_one(worker, anchor, selector_pins + anchor)
    start = worker.index('[ -d "$STATE" ]')
    stop = worker.index("# Require an intact matched writer pair", start)
    preflight = worker[start:stop].replace('"$STATE', '"$OLD_STATE').replace('"$RT', '"$OLD_RT')
    preflight = replace_one(preflight, 'absent "$OLD_STATE.previous-$tag" && absent "$OLD_RT.previous-$tag" || fail BACKUP_PATH_EXISTS',
                            'absent "$STATE" && absent "$RT" || fail TARGET_GENERATION_ALREADY_PRESENT')
    preflight = replace_one(preflight, 'regular "$OLD_STATE/armed" && [ "$(sed -n \'2p\' "$OLD_STATE/armed")" = EYE ] || fail SAME_GENERATION_REQUIRED',
                            '''regular "$OLD_STATE/armed" && regular "$OLD_RT/capture-origin" &&
    [ "$(sed -n '2p' "$OLD_STATE/armed")" = EYE ] &&
    "$OPS" same "$OLD_STATE/armed" "$OLD_RT/capture-origin" || fail ACTIVE_GENERATION_MISMATCH''')
    preflight = replace_one(preflight, 'link_new=0\nif absent "$link"; then link_new=1\nelse [ -L "$link" ] && [ "$(readlink "$link")" = /etc/systemd/system/nxl-full040-resume.service ] || fail FOREIGN_RESUME_LINK; fi\n',
                            '''[ -L "$link" ] && [ "$(readlink "$link")" = /etc/systemd/system/nxl-full040-resume.service ] || fail FOREIGN_RESUME_LINK
''')
    end = worker.index('capacity=$(cat ', stop)
    worker = worker[:start] + preflight + f'''for conf in "$ETC/hibernation.service.d/"*.conf; do
    [ "$conf" = "$ETC/hibernation.service.d/75-nxl-full040-prewrite.conf" ] || fail FOREIGN_CAPTURE_OR_RESUME_DROPIN
done
pin "$ETC/hibernation.service.d/75-nxl-full040-prewrite.conf" {old["capture-profile.conf"]} &&
    pin "$task_root/usr/bin/hibernation.sh" {old["hibernation.sh"]} &&
    pin "$task_root/usr/bin/make_snapshot.sh" {old["make_snapshot.sh"]} &&
    pin "$unit" {old["nxl-full040-resume.service"]} &&
    absent "$ETC/multi-user.target.wants/nx-on-wake.service" || fail INSTALLED_CAPTURE_IDENTITY
''' + "".join(f'pin "$OLD_RT/{name}" {old[name]} || fail INSTALLED_GENERATION_MISMATCH\n'
                for name in ("nxl-eye-af.so", "wake.sh", "verify-before-capture.sh", "collect-resume.sh")) + '''"$CTL" --all show hib_post.service -p Wants > "$REPORT/installed-wants.txt" &&
    tr '= ' '\\n\\n' < "$REPORT/installed-wants.txt" | grep -Fqx nxl-full040-resume.service || fail RESUME_DEPENDENCY_NOT_LOADED
"$CTL" --all show nxl-full040-resume.service -p ExecStart > "$REPORT/installed-entry.txt" &&
    grep -Fq '/usr/local/lib/nxl-full040/wake.sh' "$REPORT/installed-entry.txt" || fail UNIFIED_ENTRY_NOT_SELECTED
printf 'previous_runtime=eye-yunet2-direct\\neye_policy=primary-v2\\n'
''' + worker[end:]

    start = worker.index('mv "$STATE" "$STATE.previous-')
    end = worker.index("root_back || fail ROOT_REPROTECT", start)
    candidate = '''mkdir "$STATE" || fail STATE_CREATE
state_new=1
mkdir "$RT" || fail RUNTIME_CREATE
rt_new=1
chmod 755 "$RT" || fail RUNTIME_CREATE
for f in fileops.arm claim.arm; do put "$PKG/$f" "$STATE/$f" 755 || fail STATE_INSTALL; done
for f in launch.sh dispatch.sh full-menu.sh wake.sh collect-resume.sh fileops.arm legacy-launch.sh legacy-wake.sh; do
    put "$PKG/$f" "$RT/$f" 755 || fail "RUNTIME_FILE_$f"
done
put "$PKG/nxl-eye-af.so" "$RT/nxl-eye-af.so" 644 || fail EYE_LIBRARY_INSTALL
(set -C; printf '%s\\nEYE\\n%s\\n' "$before" "$leaf" > "$STATE/armed") &&
    put "$STATE/armed" "$RT/capture-origin" 600 || fail ARM_GENERATION
replace_file "$PKG/verify-before-capture.sh" usr/local/lib/nxl-full040/verify-before-capture.sh 755 &&
    replace_file "$PKG/launch-select.sh" usr/local/lib/nxl-full040/launch.sh 755 &&
    replace_file "$PKG/wake-select.sh" usr/local/lib/nxl-full040/wake.sh 755 ||
    fail CANDIDATE_BOOT_CONFIG_INSTALL
# The original service paths stay in place. Both generations retain their own
# library and state, including the existing launch claim for the old snapshot.
"$OPS" same "$STATE/armed" "$RT/capture-origin" &&
    "$OPS" same "$OLD_STATE/armed" "$OLD_RT/capture-origin" || fail CAPTURE_GENERATION_SPLIT
'''
    return worker[:start] + candidate + worker[end:]


def prepare():
    base_manifest = BASE / "rebuild-checksums.json"
    if sha(base_manifest) != BASE_MANIFEST:
        raise ValueError("Previous field manifest differs")
    base_pins = json.loads(base_manifest.read_text("utf-8"))
    old = {path.removeprefix(BASE_FOLDER + "/"): digest for path, digest in base_pins.items()}
    files = {name: (BASE / BASE_FOLDER / name).read_bytes() for name in old}
    for name in old:
        if sha(BASE / BASE_FOLDER / name) != old[name]:
            raise ValueError(f"Previous field artifact differs: {name}")
    library = ARTIFACT / "nxl-eye-af-yunet.so"
    library_hash = sha(library)
    (loaded,) = json.loads((ARTIFACT / "loader/loader-validation.json").read_text("utf-8"))
    arm = json.loads((ARTIFACT / "arm-validation.json").read_text("utf-8"))
    if (loaded["sha256"] != library_hash or loaded["return"] != 0 or not loaded["wrapper_bound"]
            or loaded["relocation_gap_bytes"] != 0 or arm["arm_shared_sha256"] != library_hash
            or arm["slow_trigger_ms_exclusive"] != 20 or not arm["multiface_and_rate_fixtures_each_backend"]):
        raise ValueError("The exact candidate must pass both ARM checks")
    if b"policy=primary-v2" not in library.read_bytes() or b"SDIC_EyeDetection" in library.read_bytes():
        raise ValueError("The Primary v2 module is required")
    PKG.mkdir(parents=True, exist_ok=False)
    text = lambda name: files[name].decode("ascii")
    for name, blob in files.items():
        write(name, blob)
    write("nxl-eye-af.so", library.read_bytes())
    for name in ("dispatch.sh", "full-menu.sh"):
        write(name, isolate(text(name)))
    for name, selector in (("launch", LAUNCH_SELECT), ("wake", WAKE_SELECT)):
        write(f"legacy-{name}.sh", files[f"{name}.sh"])
        write(f"{name}-select.sh", selector.replace("@NEW_RT@", NEW_RT).replace("@NEW_STATE@", NEW_STATE))
    write("launch.sh", replace_one(isolate(text("launch.sh")), '    mapped=$APP',
                                  f'    "{NEW_RT}/fileops.arm" same "$STATE/armed" '
                                  f'"{NEW_RT}/capture-origin" || return 1\n    mapped=$APP'))
    collector = replace_one(text("collect-resume.sh"), " model=yunet32-int8-v1", " policy=primary-v2 model=yunet32-int8-v1")
    write("collect-resume.sh", collector)
    wake = isolate(text("wake.sh"))
    for name in ("nxl-eye-af.so", "full-menu.sh", "collect-resume.sh"):
        wake = replace_one(wake, old[name], sha(PKG / name))
    write("wake.sh", wake)
    write("verify-before-capture.sh", update_capture_checker(old, text("verify-before-capture.sh")))
    write("worker.sh", update_worker(text("worker.sh"), old))
    entry = replace_one(text("activation/install.sh"), BASE_FOLDER, FOLDER)
    write("activation/install.sh", replace_one(entry, old["worker.sh"], sha(PKG / "worker.sh")))
    (BUNDLE / "rebuild-checksums.json").write_text(json.dumps(card_pins(), indent=2) + "\n", encoding="utf-8")


def configure_package():
    package.BUILD = BUILD
    package.BUNDLE = BUNDLE
    package.PKG = PKG
    package.FOLDER = FOLDER
    package.PREVIOUS_BUILD = ARTIFACT / "loader"
    package.ZIP = ZIP
    package.card_pins = card_pins
    package.TEST_DOC = Path(__file__).with_name("TEST_Primary_zh.md")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("prepare", "package", "stage"))
    parser.add_argument("--card", type=Path)
    parser.add_argument("--serial", type=lambda value: int(value.replace("-", ""), 16))
    args = parser.parse_args()
    configure_package()
    if args.mode == "prepare":
        prepare()
    else:
        checks = json.loads((BUILD / "upgrade-validation.json").read_text("utf-8"))
        staged = package.validation()
        if checks["payload"] != staged["payload"]:
            raise ValueError("Tested payload differs from the staged candidate")
        staged["scope"] = "Original ARM loader and service manager, production shell with simulated disk/camera endpoints."
        staged["primary_upgrade"] = {key: value for key, value in checks.items() if key != "payload"}
        package.validation = lambda: staged
        if args.mode == "package":
            package.make_zip()
        elif args.card is None or args.serial is None:
            parser.error("stage requires the verified card and volume serial")
        else:
            package.stage(args.card, args.serial)

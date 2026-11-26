"""Bind the already tested YuNet engine to the existing NX500 0.4.0 snapshot workflow."""

from hashlib import sha256
import json
from pathlib import Path
import re
from zipfile import ZipFile


HERE = Path(__file__).resolve().parent
WORK = HERE.parents[4] / "nx500-040-eye-cnn-work"
EYE = HERE.parent.parent / "eye-af"
PREVIOUS_BUILD = WORK / "yunet-resume-repair"
BUILD = WORK / "yunet-resume-direct"
BUNDLE = BUILD / "unified-rebuild"
FOLDER = "nxl-full040-yunet2-direct"
PKG = BUNDLE / FOLDER
RELEASE_ZIP = WORK / "NX500_Full_Mod_EyeAF_0.4.0_published.zip"
MNP_ZIP = WORK / "dist/NX500_0.4.0_Unified_R1_EyeMNP_1.zip"
YUNET1_ZIP = WORK / "dist/NX500_0.4.0_Unified_R1_EyeYuNet_1_Rebuild.zip"
BASE = "nxl-full040-rebuild1"
PREVIOUS = "nxl-full040-mnp1"
YUNET1 = "nxl-full040-yunet1"
PIN_RELEASE = "49de7be7e19899526556178e386b5beafb43c2049f43ebf51b7b41e33e5fe766"
PIN_MNP = "2f086036308c11350c8bcfac498ed433cd38442c6f4562e754fb1c6b9a19535e"
PIN_YUNET1 = "0a47560383cb5b454894840c653c7157fd8db0bf100ab33d28e9f82f37b1253e"
PARTS = ("nxl-eye-af.so", "wake.sh", "verify-before-capture.sh", "collect-resume.sh")


def sha(path):
    return sha256(path.read_bytes()).hexdigest()


def replace_one(text, before, after):
    if text.count(before) != 1:
        raise ValueError(f"Expected a unique anchor: {before}")
    return text.replace(before, after)


def payload(archive, directory):
    prefix = "unified-rebuild/"
    data = {}
    for full, digest in json.loads(archive.read(prefix + "rebuild-checksums.json")).items():
        if not full.startswith(directory + "/"):
            raise ValueError(f"Unexpected payload path: {full}")
        name = full.removeprefix(directory + "/")
        if not re.fullmatch(r"(?:activation/)?[A-Za-z0-9_.-]+", name):
            raise ValueError(f"Unexpected payload path: {full}")
        blob = archive.read(prefix + full)
        if sha256(blob).hexdigest() != digest:
            raise ValueError(f"Archived payload differs: {full}")
        data[name] = blob
    return data


def adapt_wake(old):
    body = (PKG / "wake.sh").read_text("ascii")
    before = '''    if [ "$(readlink "$task_root/proc/$pid/exe" 2>/dev/null)" = /usr/apps/com.samsung.di-camera-app/bin/di-camera-app ] &&
        nxl_require_normal_mode >/dev/null 2>&1; then break; fi
    [ "$n" -lt 45 ] || stop CAMERA_NOT_READY'''
    after = '''    reason=CAMERA_PROCESS_NOT_READY
    if [ "$(readlink "$task_root/proc/$pid/exe" 2>/dev/null)" = /usr/apps/com.samsung.di-camera-app/bin/di-camera-app ]; then
        if message=$(nxl_require_normal_mode 2>&1); then break; fi
        case "$message" in
            'Factory script mode (dfmsd) is active.'*) reason=FACTORY_MODE_ACTIVE;;
            *) reason=PROCESS_STATE_UNAVAILABLE;;
        esac
    fi
    [ "$n" -lt 45 ] || stop "$reason"'''
    body = replace_one(body, before, after)
    body = replace_one(body, '/bin/sh "$rt/collect-resume.sh" </dev/null',
                      '/bin/sh "$rt/collect-resume.sh" "$pid" "$leaf" </dev/null')
    for name in ("collect-resume.sh", "nxl-eye-af.so"):
        body = replace_one(body, old[name], sha(PKG / name))
    (PKG / "wake.sh").write_bytes(body.encode("ascii"))
    checker = PKG / "verify-before-capture.sh"
    body = checker.read_text("ascii")
    for name in ("wake.sh", "nxl-eye-af.so"):
        body = replace_one(body, old[name], sha(PKG / name))
    checker.write_bytes(body.encode("ascii"))


def adapt_worker(old, r1, yunet1):
    path = PKG / "worker.sh"
    body = replace_one(path.read_text("ascii"), PREVIOUS, FOLDER)
    for name in PARTS:
        before = f'pin "$PKG/{name}" {old[name]} || fail PAYLOAD_IDENTITY'
        body = replace_one(body, before, before.replace(old[name], sha(PKG / name)))
    body = replace_one(body, f'pin "$PKG/lcd-observe.arm" {old["lcd-observe.arm"]} || fail PAYLOAD_IDENTITY\n', "")
    body = replace_one(body, f'pin "$RT/nxl-eye-af.so" {r1["nxl-eye-af.so"]} || fail INSTALLED_GENERATION_MISMATCH\n', "")
    body = replace_one(body, "    writers=stock",
                       f'    pin "$RT/nxl-eye-af.so" {r1["nxl-eye-af.so"]} || fail INSTALLED_GENERATION_MISMATCH\n'
                       "    writers=stock\n    generation=legacy")
    for name in ("wake.sh", "verify-before-capture.sh"):
        body = replace_one(body, f'        pin "$RT/{name}" {r1[name]} &&\n', "")
    variants = []
    for word, pins, label in (("if", r1, "unified-r1"), ("elif", old, "eye-mnp1"),
                              ("elif", yunet1, "eye-yunet1")):
        checks = [f'pin "$RT/{name}" {pins[name]}' for name in PARTS]
        variants.append(f"    {word} " + " &&\n       ".join(checks) + f"; then\n        generation={label}")
    body = replace_one(body, "    writers=unified-r1",
                       "\n".join(variants) + "\n    else\n        fail INSTALLED_GENERATION_MISMATCH\n    fi\n"
                       "    writers=unified-r1")
    body = replace_one(body, r"printf 'previous_writers=%s\neye_backend=mnp-s8-v1\n' " + '"$writers"',
                       r"printf 'previous_writers=%s\nprevious_runtime=%s\neye_backend=yunet32-int8-v1\nlink_layout=contiguous-rel-plt\n' "
                       + '"$writers" "$generation"')
    path.write_bytes(direct_worker(body).encode("ascii"))
    entry = PKG / "activation/install.sh"
    body = replace_one((HERE / "install-direct.sh").read_text("ascii"), "@WORKER_SHA@", sha(path))
    entry.write_bytes(body.encode("ascii"))


def direct_worker(body):
    body = replace_one(body, 'tag=$(date +%s)_$$\nleaf=FAST_START_EYE_AF_ROOTLIB_$tag', '''[ "$#" = 1 ] || exit 64
leaf=$1
tag=${leaf#FAST_START_EYE_AF_ROOTLIB_}
[ "$tag" != "$leaf" ] && printf '%s\\n' "$tag" | grep -Eq '^[0-9]+_[0-9]+$' || exit 64''')
    body = replace_one(body, 'idle() {\n    [ "$(cat "$USB" 2>/dev/null)" = 0 ] && absent', '''card_up() {
    [ "$(cat "$USB" 2>/dev/null)" = 0 ] &&
        awk '$1=="/dev/mmcblk1p1" && ($2=="/mnt/mmc" || $2=="/opt/storage/sdcard") && $4~/^rw(,|$)/ {ok=1} END {exit !ok}' "$task_root/proc/mounts"
}
idle() {
    card_up && absent''')
    body = replace_one(body, '''[ "$(cat "$USB" 2>/dev/null)" = 0 ] && [ ! -L "$SD/NXDEMO51" ] && mkdir -p "$SD/NXDEMO51" && mkdir "$REPORT" || exit 70
exec > "$REPORT/prepare.log" 2>&1''', '''card_up && [ ! -L "$SD/NXDEMO51" ] && [ -d "$REPORT" ] && [ ! -L "$REPORT" ] &&
    [ -d "$REPORT/entry" ] && [ ! -L "$REPORT/entry" ] &&
    (set -C; : > "$REPORT/prepare.log") || exit 70
exec >> "$REPORT/prepare.log" 2>&1''')
    body = replace_one(body, '''regular "$PKG/request" && [ "$(cat "$PKG/request")" = REBUILD ] || fail WRONG_REQUEST
for f in info.tg nx_cs.adj install.sh; do
    regular "$SD/$f" && "$OPS" same "$SD/$f" "$PKG/activation/$f" || fail TRIGGER_IDENTITY
done
mkdir "$REPORT/entry" || fail TRIGGER_ARCHIVE
for f in info.tg nx_cs.adj install.sh; do mv "$SD/$f" "$REPORT/entry/$f" || fail TRIGGER_ARCHIVE; done''', '''printf 'source=direct-sd-entry\\nworker_pid=%s\\n' "$$"
regular "$PKG/request" && [ "$(cat "$PKG/request")" = REBUILD ] || fail WRONG_REQUEST
for f in info.tg nx_cs.adj install.sh; do
    absent "$SD/$f" && regular "$REPORT/entry/$f" && regular "$PKG/activation/$f" &&
        "$OPS" same "$REPORT/entry/$f" "$PKG/activation/$f" || fail TRIGGER_IDENTITY
done''')
    body = replace_one(body, 'if [ "$(cat "$USB" 2>/dev/null)" = 0 ]; then\n        printf \'exit=',
                       'if card_up; then\n        printf \'exit=')
    body = install_runtime_without_move(body)
    body = replace_one(body, '"$PKG/nxl-menu" --fast-start-capture || fail NOTICE_NOT_SHOWN\n', '')
    body = replace_one(body, 'idle || fail CAMERA_BECAME_BUSY_BEFORE_RESCUE\n', '''idle || fail CAMERA_BECAME_BUSY_BEFORE_RESCUE
cd / || fail FINAL_CWD
''')
    return replace_one(body, 'sync\nhanded=1\ncd / || fail FINAL_CWD\n', '''# The toast only gives the operator a visual cue. It may be cut short by rescue.
"$PKG/nxl-menu" --fast-start-capture </dev/null >/dev/null 2>&1 &
sync
handed=1
''')


def install_runtime_without_move(body):
    begin = body.index("free=$(")
    end = body.index("idle || fail CAMERA_BECAME_BUSY\n", begin)
    body = body[:begin] + body[end:]
    body = replace_one(body, '''while read -r wanted file extra; do
    case "$file" in ''|/*|*..*|*\\\\*) fail INVALID_MODULE_MANIFEST;; esac
    [ -z "$extra" ] && pin "$mod/$file" "$wanted" || fail FULL_MODULE_FILE_MISMATCH
done < "$mod/state/module.manifest"''', '''pin "$mod/process.sh" cf0f12a1b5eba46ae9ec46c2d3b98ff16aed93582d29d378e157ea5d32589867 ||
    fail FULL_MODULE_FILE_MISMATCH''')
    body = replace_one(body, 'rt_saved=0; ', "")
    body = replace_one(body, '[ "$changed" = 1 ] || [ "$rt_saved" = 1 ]', '[ "$changed" = 1 ] || [ "$rt_new" = 1 ]')
    body = replace_one(body, '        if [ "$rt_saved" = 1 ]; then mv "$RT.previous-$tag" "$RT" || printf \'ROLLBACK_RUNTIME_FAILED\\n\'; fi\n', "")
    body = replace_one(body, '    absent "$dst.pre-full040-$tag" || return 1', '''    absent "$dst.pre-full040-$tag" && absent "$dst.new.$$" || return 1
    if ! absent "$dst"; then
        regular "$dst" && ln "$dst" "$dst.pre-full040-$tag" || return 1
    fi''')
    body = replace_one(body, '''    if ! absent "$dst"; then
        regular "$dst" && mv "$dst" "$dst.pre-full040-$tag" || return 1
        # Preserve ownership, mode and security xattrs of existing OEM files.''', '''    if regular "$dst.pre-full040-$tag"; then
        # Keep the old inode available for rollback until the new file is verified.''')
    body = replace_one(body, '''mv "$RT" "$RT.previous-$tag" || fail RUNTIME_ARCHIVE
rt_saved=1
mkdir "$RT" || fail RUNTIME_CREATE
rt_new=1
chmod 755 "$RT" || fail RUNTIME_CREATE
for f in launch.sh dispatch.sh readout full-menu.sh wake.sh collect-resume.sh fileops.arm verify-before-capture.sh; do
    put "$PKG/$f" "$RT/$f" 755 || fail RUNTIME_INSTALL
done
put "$PKG/nxl-eye-af.so" "$RT/nxl-eye-af.so" 644 || fail EYE_LIBRARY_INSTALL
(set -C; printf '%s\\nEYE\\n%s\\n' "$before" "$leaf" > "$STATE/armed") &&
    put "$STATE/armed" "$RT/capture-origin" 600 || fail ARM_GENERATION''', '''if absent "$RT"; then
    mkdir "$RT" || fail RUNTIME_CREATE
    rt_new=1
    chmod 755 "$RT" || fail RUNTIME_CREATE
fi
[ -d "$RT" ] && [ ! -L "$RT" ] || fail RUNTIME_PATH_CHANGED
for f in launch.sh dispatch.sh readout full-menu.sh wake.sh collect-resume.sh fileops.arm verify-before-capture.sh nxl-eye-af.so; do
    if regular "$RT/$f" && "$OPS" same "$PKG/$f" "$RT/$f"; then continue; fi
    perm=755
    [ "$f" != nxl-eye-af.so ] || perm=644
    replace_file "$PKG/$f" "usr/local/lib/nxl-full040/$f" "$perm" || fail "RUNTIME_FILE_$f"
done
(set -C; printf '%s\\nEYE\\n%s\\n' "$before" "$leaf" > "$STATE/armed") &&
    replace_file "$STATE/armed" usr/local/lib/nxl-full040/capture-origin 600 || fail ARM_GENERATION''')
    body = replace_one(body, '"$CTL" daemon-reload || fail MANAGER_RELOAD', '''if [ "$writers" = stock ] || [ "$link_added" = 1 ] || [ "$master_detached" = 1 ]; then
    "$CTL" daemon-reload || fail MANAGER_RELOAD
fi''')
    return body


def card_pins():
    return {p.relative_to(BUNDLE).as_posix(): sha(p) for p in sorted(PKG.rglob("*")) if p.is_file()}


def main():
    if sha(RELEASE_ZIP) != PIN_RELEASE or sha(MNP_ZIP) != PIN_MNP or sha(YUNET1_ZIP) != PIN_YUNET1:
        raise ValueError("Previous release archive identity differs")
    with ZipFile(MNP_ZIP) as archive:
        base = payload(archive, PREVIOUS)
    with ZipFile(RELEASE_ZIP) as archive:
        r1 = {name: sha256(archive.read(f"unified-rebuild/{BASE}/{name}")).hexdigest() for name in PARTS}
    with ZipFile(YUNET1_ZIP) as archive:
        yunet1 = {name: sha256(archive.read(f"unified-rebuild/{YUNET1}/{name}")).hexdigest() for name in PARTS}
    old = {name: sha256(blob).hexdigest() for name, blob in base.items()}
    for name, blob in base.items():
        if name != "lcd-observe.arm":
            file = PKG / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(blob)
    library = (EYE / "out/nxl-eye-af-yunet.so").read_bytes()
    if b"SDIC_EyeDetection" in library or b"model=yunet32-int8-v1" not in library or b"no_face_frames=" not in library:
        raise ValueError("First build the intended YuNet camera module")
    (PKG / "nxl-eye-af.so").write_bytes(library)
    (PKG / "collect-resume.sh").write_bytes((HERE / "collect-resume.sh").read_text("ascii").encode("ascii"))
    adapt_wake(old)
    adapt_worker(old, r1, yunet1)
    pins = card_pins()
    if len(pins) != len(old) - 1:
        raise ValueError("Unexpected additional file in rebuild package")
    (BUNDLE / "rebuild-checksums.json").write_text(json.dumps(pins, indent=2) + "\n", encoding="utf-8")
    print(f"{len(pins)} files, live library SHA-256 {sha(PKG / 'nxl-eye-af.so')}")


if __name__ == "__main__":
    main()

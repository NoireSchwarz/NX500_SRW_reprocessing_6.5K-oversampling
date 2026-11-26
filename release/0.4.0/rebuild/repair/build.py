"""Build two guarded NX500 1.12 installers from the exact released/field payloads."""

import argparse
from hashlib import sha256
import json
from pathlib import Path, PurePosixPath
import re
from zipfile import ZIP_DEFLATED, ZipFile

HERE = Path(__file__).resolve().parent
RELEASE = HERE.parents[1]
PUBLISHED_SHA = "49de7be7e19899526556178e386b5beafb43c2049f43ebf51b7b41e33e5fe766"
FOCUS_SHA = "01e754be2e9577ebde190dd32fd8091299753cf16d382112de968b85f151e150"
FOCUS_LIB = "0955e5d90a61d33740c4fa18150e7499db4e959a536cd4c84a1eb04b3e587cbb"
R1_PREFIX = "unified-rebuild/nxl-full040-rebuild1/"
FOCUS_PREFIX = "unified-rebuild/nxl-full040-yunet-focus2/"
VARIANTS = {"EYE28": "nxl-full040-r11-eye28", "YUNET": "nxl-full040-r11-yunet"}
RUNTIME = ("launch.sh", "dispatch.sh", "full-menu.sh", "wake.sh", "collect-resume.sh", "fileops.arm", "nxl-eye-af.so")
CANON = "usr/local/lib/nxl-full040"
UNITS = "etc/systemd/system"
CAPTURE_CONF = UNITS + "/di-camera-app.service.d/68-nxl-full040.conf"
PROFILE = UNITS + "/hibernation.service.d/75-nxl-full040-prewrite.conf"


def digest(data):
    return sha256(data).hexdigest()


def replace(text, original, new):
    if text.count(original) != 1:
        raise ValueError(f"Unexpected reference source at {original[:80]!r}")
    return text.replace(original, new)


def put(root, name, data):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
        raise ValueError(name)
    dest = root.joinpath(*path.parts)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("xb") as stream:
        stream.write(data.encode("utf-8") if isinstance(data, str) else data)


def read_inputs(published, focus):
    for path, expected in ((published, PUBLISHED_SHA), (focus, FOCUS_SHA)):
        if digest(path.read_bytes()) != expected:
            raise ValueError(f"Reference archive identity differs: {path.name}")
    with ZipFile(published) as z, ZipFile(focus) as y:
        if z.testzip() or y.testzip():
            raise ValueError("Reference archive CRC failure")
        all_files = {name: z.read(name) for name in z.namelist() if not name.endswith("/")}
        r1 = {name.removeprefix(R1_PREFIX): data for name, data in all_files.items() if name.startswith(R1_PREFIX)}
        latest = {name.removeprefix(FOCUS_PREFIX): y.read(name) for name in y.namelist() if name.startswith(FOCUS_PREFIX)}
        latest["YuNet-LICENSE.txt"] = y.read("YuNet-LICENSE.txt")
    if digest(latest["nxl-eye-af.so"]) != FOCUS_LIB:
        raise ValueError("Focus2 library differs from the tested binary")
    return all_files, r1, latest


def protected_header(source):
    check = "/bin/sh /usr/local/lib/nxl-full040/verify-before-capture.sh"
    write_gate = f"{check} --at-image-write || exit 1\n"
    cold_ready = "    /bin/touch /tmp/.hib_ready\n    exit 0\n"
    source = replace(source, "echo timer > /sys/class/leds/card/trigger\n", "")
    source = replace(source, 'echo "Starting hibernation ... "\n',
                     f'if ! {check} --at-preparation; then\n{cold_ready}fi\necho "Starting hibernation ... "\n')
    return replace(source, write_gate, f"if ! {check} --at-image-write; then\n"
                   "    /sbin/swapoff /dev/zram0 || :\n" + cold_ready +
                   "fi\necho timer > /sys/class/leds/card/trigger\n")


def guarded_dispatch(source, generation):
    # A failed rescue must disarm the root-backed prewrite gate too. /opt/usr may
    # be unavailable when hibernation.service starts on the next boot.
    path = f'$task_root/usr/local/lib/{generation}/capture-origin'
    return replace(source, '    mv "$STATE/armed" "$STATE/rescue-rejected"\n',
                   f'    gate={path}\n'
                   '    ops=${gate%/*}/fileops.arm\n'
                   '    if [ -f "$gate" ] && [ ! -L "$gate" ] && "$ops" same "$STATE/armed" "$gate"; then\n'
                   '        mode=$(awk \'$2=="/" {split($4,a,","); print a[1]}\' "$task_root/proc/mounts")\n'
                   '        if [ "$mode" = rw ] || { [ "$mode" = ro ] && "$task_root/bin/mount" -o remount,rw /; }; then\n'
                   '            if mv "$gate" "$gate.rescue-rejected"; then mv "$STATE/armed" "$STATE/rescue-rejected"; fi\n'
                   '            [ "$mode" != ro ] || "$task_root/bin/mount" -o remount,ro /\n'
                   '        fi\n'
                   '    fi\n')


def checker(source, generation, files):
    def pin(path, content):
        return (f'[ -f "$root/{path}" ] && [ ! -L "$root/{path}" ] && '
                f'[ "$("$ops" sha256 "$root/{path}")" = {digest(content)} ] || stop UNIFIED_PAYLOAD_MISMATCH\n')

    source = replace(source, '1/--record|1/--at-image-write)', '1/--record|1/--at-preparation|1/--at-image-write)')
    source = replace(source, '[ "$1" != --at-image-write ] || stage=image-header\n',
                     '[ "$1" != --at-image-write ] || stage=image-header\n'
                     '        [ "$1" != --at-preparation ] || stage=image-preparation\n')
    source = replace(source, '        image-header)', '        image-header|image-preparation)')
    source = replace(source, '[ "$stage" != image-header ] || writer_reason=WRITER_IS_NOT_CURRENT_STOCK_MAIN',
                     '[ "$stage" = before-capture ] || writer_reason=WRITER_IS_NOT_CURRENT_STOCK_MAIN')
    a = source.index('ops=$root/usr/local/lib/nxl-full040/fileops.arm\n')
    b = source.index('{ IFS= read -r old_boot', a)
    script = f'ops=$root/usr/local/lib/{generation}/fileops.arm\n'
    for name in RUNTIME:
        script += pin(f"usr/local/lib/{generation}/{name}", files[name])
    for name in ("launch", "wake"):
        script += pin(f"{CANON}/{name}.sh", files[f"{name}-select.sh"])
    script += pin(f"{UNITS}/nxl-full040-resume.service", files["nxl-full040-resume.service"])
    script += f'origin=$root/usr/local/lib/{generation}/capture-origin\n'
    script += '[ -f "$origin" ] && [ ! -L "$origin" ] || stop CAPTURE_ORIGIN_MISSING\n'
    source = source[:a] + script + source[b:]
    return replace(source, 'interface=full040-unified-1\\norigin_boot_id=%s\\ncapture_report=%s\\n',
                   f'interface=full040-repair-1\\norigin_boot_id=%s\\ncapture_report=%s\\neye_build={generation}\\n')


def build_variant(all_files, r1, latest, variant):
    generation = VARIANTS[variant]
    seed = r1 if variant == "EYE28" else latest
    # Carry forward the tested AF library verbatim. Only startup paths and their integrity pins change.
    def relocate(data):
        return re.sub(rb'(?<![\w-])nxl-full040(?![\w-])|nxl-full040-rebuild1|nxl-full040-yunet-focus2',
                      generation.encode("ascii"), data)

    data = {f: r1[f] for f in ("allowed-overrides.txt", "nxl-menu", "lcd-observe.arm", "fileops.arm", "claim.arm",
                              "nxl-full040-resume.service", "make_snapshot.sh", "activation/info.tg", "activation/nx_cs.adj")}
    data["nxl-eye-af.so"] = seed["nxl-eye-af.so"]
    data["capture.conf"] = all_files["nxl-full040/capture.conf"]
    data["capture-profile.conf"] = r1["capture-profile.conf"].replace(b"ExecStartPre=", b"ExecStartPre=-")
    data["hibernation.sh"] = protected_header(r1["hibernation.sh"].decode("ascii")).encode("ascii")
    for name in ("launch.sh", "dispatch.sh", "full-menu.sh", "collect-resume.sh", "loader-observe.sh"):
        original = all_files["nxl-full040/loader-observe.sh"] if name == "loader-observe.sh" else seed[name]
        data[name] = relocate(original)
    data["dispatch.sh"] = guarded_dispatch(data["dispatch.sh"].decode("ascii"), generation).encode()
    for action in ("launch", "wake"):
        data[f"{action}-select.sh"] = (HERE / "select.sh").read_text("utf-8").replace("@ACTION@", action).replace(
            "@GENERATION@", generation).encode()
    wake = relocate(seed["wake.sh"]).decode("ascii")
    for name in ("full-menu.sh", "collect-resume.sh"):
        wake = replace(wake, digest(seed[name]), digest(data[name]))
    data["wake.sh"] = replace(wake, 'interface=full040-unified-1\\nsource=',
                              f'interface=full040-repair-1\\neye_build={generation}\\nsource=').encode()
    data["verify-before-capture.sh"] = checker(r1["verify-before-capture.sh"].decode("ascii"), generation, data).encode()

    old_worker = r1["worker.sh"].decode("ascii")
    stock = {path: pin for path, pin in re.findall(r'pin "\$task_root(/[^"\n]*)" ([a-f0-9]{64}) \|\| fail STOCK_IDENTITY_CHANGED', old_worker)}
    if len(stock) != 14:
        raise ValueError("Unexpected stock identities in R1")
    stock_writers = {p: stock.pop(p) for p in ("/usr/bin/hibernation.sh", "/usr/bin/make_snapshot.sh")}
    stock_sleep = re.search(r'pin "\$stock_sleep" ([a-f0-9]{64})', all_files["nxl-full040/worker.sh"].decode("ascii"))[1]
    core = re.search(r'case "\$core" in [a-f0-9|]+\)', old_worker)[0]
    r1_pins = {f"{CANON}/{n}": digest(r1[n]) for n in (*RUNTIME, "verify-before-capture.sh", "readout")}
    r1_pins.update({name: digest(r1[filename]) for name, filename in (
        (PROFILE, "capture-profile.conf"), (f"{UNITS}/nxl-full040-resume.service", "nxl-full040-resume.service"),
        ("usr/bin/hibernation.sh", "hibernation.sh"), ("usr/bin/make_snapshot.sh", "make_snapshot.sh"),
        ("opt/usr/nxl-full040/claim.arm", "claim.arm"), ("opt/usr/nxl-full040/fileops.arm", "fileops.arm"))})
    r1_pins[CAPTURE_CONF] = digest(data["capture.conf"])

    def pins(entries, folder, reason):
        return "\n".join(f'pin "{folder}/{name}" {pin} || stop {reason}' for name, pin in sorted(entries.items()))

    manifests = "|".join(("e3520acc8a3adf788cf6bf1a5a9a927ee55ab5fd6cf296ffc35ca79286baa167",
                          digest(all_files["module/nx-lut-demo/module.manifest"])))
    values = {"GENERATION": generation, "MANIFESTS": manifests, "PAYLOAD_CHECKS": pins(
        {n: digest(b) for n, b in data.items()}, "$PKG", "PAYLOAD_IDENTITY"),
        "STOCK_CHECKS": pins({p[1:]: v for p, v in stock.items()}, "$root", "STOCK_IDENTITY_CHANGED") +
        f'\nstock_sleep=$(readlink -f "$root/bin/sleep") && pin "$stock_sleep" {stock_sleep} || stop STOCK_SLEEP_CHANGED\n'
        'core=$("$OP" sha256 "$root/usr/lib/libmmfcore.so") || stop CORE_UNREADABLE\n' + core + ' ;; *) stop CORE_UNRECOGNIZED;; esac',
        "CLEAN_CHECKS": pins({p[1:]: v for p, v in stock_writers.items()}, "$root", "STOCK_WRITER_CHANGED"),
        "R1_CHECKS": pins(r1_pins, "$root", "R1_PAYLOAD_CHANGED"),
        "OLD_MASTER": digest(all_files["nxl-full040/wake-master"]), "OLD_MASTER_UNIT": digest(all_files["nxl-full040/wake-unit"])}
    worker = (HERE / "worker.sh").read_text("utf-8")
    for name, value in values.items():
        worker = worker.replace(f"@{name}@", value)
    if re.search(r"@[A-Z_]+@", worker):
        raise ValueError("Unexpanded worker template")
    data["worker.sh"] = worker.encode()
    entry = relocate(all_files["nxl-full040/activation/install.sh"]).decode("ascii")
    entry = replace(entry, digest(all_files["nxl-full040/worker.sh"]), digest(data["worker.sh"]))
    data["activation/install.sh"] = entry.encode()
    return data


def prepare(published, focus, target):
    all_files, r1, latest = read_inputs(published, focus)
    target.mkdir()
    # No old fast-start flows or root triggers in the new release.
    retained = ("module/", "source/", "eye-v2.8/licenses/", "eye-v2.8/source/")
    for name, data in all_files.items():
        if name.startswith(retained) or name in ("gray-calibration.json", "eye-v2.8/NOTICE.md"):
            put(target, name, data)
    for variant, generation in VARIANTS.items():
        payload = build_variant(all_files, r1, latest, variant)
        for name, data in payload.items():
            put(target, f"profiles/{generation}/{name}", data)
        put(target, f"profiles/{generation}/checksums.json", json.dumps({n: digest(b) for n, b in payload.items()}, indent=2) + "\n")
    docs = {"README_zh.md": HERE / "README_zh.md", "RECOVERY_zh.md": RELEASE / "RECOVERY_zh.md",
            "AUDIT_zh.md": HERE / "AUDIT_zh.md", "install-fast-start.py": HERE / "stage.py",
            "card_files.py": HERE.parent / "coexist_rebuild.py"}
    for name, source in docs.items():
        content = source.read_bytes()
        if name == "README_zh.md":
            content = content.replace(b"../../RECOVERY_zh.md", b"RECOVERY_zh.md")
        put(target, name, content)
    put(target, "license/YuNet-MIT.txt", latest["YuNet-LICENSE.txt"])
    for name in ("build.py", "worker.sh", "select.sh", "stage.py", "test_repair.py", "test_arm.py"):
        put(target, f"source/release/0.4.0/rebuild/repair/{name}", (HERE / name).read_bytes())


def package(tree, output):
    proof = json.loads((tree / "validation.json").read_text("utf-8"))
    if not proof.get("host_passed") or not proof.get("arm_manager_passed"):
        raise ValueError("Host and ARM replay receipts are required")
    for generation, pins in proof["profiles"].items():
        directory = tree / "profiles" / generation
        if json.loads((directory / "checksums.json").read_text("utf-8")) != pins or any(
            digest((directory / name).read_bytes()) != pin for name, pin in pins.items()
        ):
            raise ValueError("Validation does not cover the current payload")
    files = {p.relative_to(tree).as_posix(): p for p in tree.rglob("*") if p.is_file() and "__pycache__" not in p.parts}
    if any(name in files for name in ("info.tg", "nx_cs.adj", "install.sh", "nx500.bin", "stage-postflash.py")):
        raise ValueError("Unexpected active trigger at the archive root")
    with ZipFile(output, "x", ZIP_DEFLATED, compresslevel=6) as z:
        for name, file in sorted(files.items()):
            z.write(file, name)
    with ZipFile(output) as z:
        if z.testzip() or set(z.namelist()) != files.keys() or any(
            digest(z.read(name)) != digest(f.read_bytes()) for name, f in files.items()
        ):
            raise ValueError("Final ZIP readback differs")
    output.with_suffix(".sha256").write_text(f"{digest(output.read_bytes())}  {output.name}\n", "ascii")


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    sub = cli.add_subparsers(dest="command", required=True)
    prep = sub.add_parser("prepare")
    prep.add_argument("published", type=Path)
    prep.add_argument("focus", type=Path)
    prep.add_argument("tree", type=Path)
    pack = sub.add_parser("package")
    pack.add_argument("tree", type=Path)
    pack.add_argument("output", type=Path)
    args = cli.parse_args()
    if args.command == "prepare":
        prepare(args.published, args.focus, args.tree)
    else:
        package(args.tree, args.output)


if __name__ == "__main__":
    main()

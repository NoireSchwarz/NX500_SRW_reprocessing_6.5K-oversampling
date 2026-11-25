"""Assemble the combined release from the verified Log117 and public Eye AF v2.8 ZIPs."""
import argparse
import hashlib
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path, PurePosixPath
import re
import struct
import zipfile


HERE = Path(__file__).resolve().parent
OLD = "nxl-eye-image28"
NEW = "nxl-full040"
OLD_CONF = "67-nxl-eye-publication.conf"
NEW_CONF = "68-nxl-full040.conf"
MODULE = "e3520acc8a3adf788cf6bf1a5a9a927ee55ab5fd6cf296ffc35ca79286baa167"
FULL_ZIP = "a09885ff7ae1ac5942ea0b83550cfb8e1a794fbeb3011f7a37a6f5f30b0b1576"
EYE_LIB = "e167e84a6be88b2fc21da13a5952a923ec7a032cb37612a11e047e60dc6c2e6e"
PREFIX = "nx-lut-demo/nx-lut-lite/"
MANIFEST = "nx-lut-demo/module.manifest"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Unexpected source at {old[:60]!r}")
    return text.replace(old, new)


def put(folder, name, data):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
        raise ValueError(name)
    dest = folder.joinpath(*path.parts)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data)


def elf_sections(blob):
    if blob[:6] != b"\x7fELF\x01\x01" or struct.unpack_from("<H", blob, 18)[0] != 40:
        raise ValueError("Expected an ARM32 little-endian ELF")
    offset = struct.unpack_from("<I", blob, 32)[0]
    stride, count, names = struct.unpack_from("<3H", blob, 46)
    sections = [struct.unpack_from("<10I", blob, offset+i*stride) for i in range(count)]
    table = sections[names]
    strings = blob[table[4]:table[4]+table[5]]
    return {strings[s[0]:].split(b"\0",1)[0].decode(): (s[4],s[5]) for s in sections}


def portable_binary(blob, section, suffixes):
    """Replace compiler paths only where the pinned ELF stores those strings."""
    offset, length = elf_sections(blob)[section]
    area = bytearray(blob[offset:offset+length])
    paths = list(re.finditer(rb"[A-Z]:\\[^\0]+", area))
    if len(paths) != len(suffixes):
        raise ValueError("Unexpected embedded producer paths")
    for match, suffix in zip(paths, suffixes):
        if not match.group().replace(b"\\",b"/").endswith(b"/"+suffix):
            raise ValueError("ELF string differs from the reference release")
        size = match.end()-match.start()
        # Preserve offsets within the assertion filename. The loader's DT_NEEDED
        # entries point at the beginning of the library string.
        value = (b"_"*(size-len(suffix)-1)+b"/"+suffix if section == ".rodata"
                 else suffix.ljust(size,b"\0"))
        area[match.start():match.end()] = value
    return blob[:offset]+area+blob[offset+length:]


def module_files(z):
    out = {n:z.read(n) for n in z.namelist() if not n.endswith("/") and (
        n.startswith(("nx-lut-demo/","looks/","LICENSE")) or n in ("info.tg","install.sh","nx_cs.adj"))}
    original = out.copy()
    menu, sharp = PREFIX+"bin/nxl-menu", PREFIX+"bin/nxl-sharp.arm"
    out[menu] = portable_binary(out[menu], ".rodata", [b"src/qrcodegen.c"])
    out[sharp] = portable_binary(out[sharp], ".dynstr", [b"libcapture-fw-prod.so",b"libmulticore-bridge.so"])
    text_names = [n for n in original if n.startswith("nx-lut-demo/") and (
        n.endswith(".sh") or n == MANIFEST) and "/ownership/" not in n]
    # References use lowercase ASCII SHA-256. Compute from original bytes at
    # every round so even multiple dependencies of a single script stay in sync.
    for _ in range(20):
        changed = {digest(original[n]).encode():digest(b).encode() for n,b in out.items() if b != original[n]}
        next_bytes = {n:re.sub(rb"(?<![0-9a-f])[0-9a-f]{64}(?![0-9a-f])",
            lambda m:changed.get(m.group(),m.group()),original[n]) for n in text_names}
        if all(out[n] == b for n,b in next_bytes.items()):
            break
        out.update(next_bytes)
    else:
        raise ValueError("Cyclic module identity references")
    for line in out[MANIFEST].decode().splitlines():
        pin, name = line.split()
        if digest(out[PREFIX+name]) != pin:
            raise ValueError(f"Module identity mismatch at {name}")
    return out


def active_sources(z, module):
    files = {}
    for prefix, target in (
        ("SOURCE/RC61411_WIFI_QUIET/src/","base/nx-lut-lite/src/"),
        ("SOURCE/RC61412_Log117/ui/","base/nx-lut-lite/src/"),
        ("SOURCE/Log117/core/src/","core/src/"), ("SOURCE/Log117/common/","common/")):
        files.update({target+n[len(prefix):]:z.read(n) for n in z.namelist() if n.startswith(prefix) and not n.endswith("/")})
    for name, data in module.items():
        if name.startswith(PREFIX) and (name.endswith((".sh",".inc",".html",".js",".tsv",".json",".txt",".md"))):
            files["base/nx-lut-lite/"+name[len(PREFIX):]] = data
    return files


def combined_eye(source, portable_manifest):
    hashes = json.loads(source.read("checksums.json"))
    old = {name.split("/", 1)[1]: source.read(name) for name in hashes}
    if any(digest(data) != hashes[f"{OLD}/{name}"] for name, data in old.items()) or digest(old["nxl-eye-af.so"]) != EYE_LIB:
        raise ValueError("Eye package identities do not match")
    data = old.copy()
    for name in ("capture.conf", "launch.sh", "dispatch.sh", "loader-observe.sh", "wake-entry", "readout", "worker.sh", "activation/install.sh"):
        data[name] = old[name].replace(OLD.encode(), NEW.encode())
    data["full-menu.sh"] = replace_once((HERE / "full-menu.sh").read_text("utf-8"), "@PUBLIC_MODULE@", portable_manifest).encode()
    readout = data["readout"].decode()
    start = readout.index('    if [ -f "$task_root/tmp/.hib_post" ] && [ -f "$WAKE" ]')
    end = readout.index("    fi", start) + len("    fi")
    readout = readout[:start] + (
        '    menu=$task_root/usr/local/lib/nxl-full040/full-menu.sh\n'
        '    if [ -f "$task_root/tmp/.hib_post" ] && [ -f "$menu" ] && [ ! -L "$menu" ] &&\n'
        f'        [ "$("$OPS" sha256 "$menu")" = {digest(data["full-menu.sh"])} ]; then\n'
        '        exec /bin/sh "$menu"\n    fi'
    ) + readout[end:]
    data["readout"] = replace_once(readout, "WAKE=$task_root/opt/usr/nx-on-wake/on-wake\n", "").encode()

    worker = data["worker.sh"].decode()
    worker = replace_once(worker, "CONF=$DROP/" + OLD_CONF, "CONF=$DROP/" + NEW_CONF)
    worker = replace_once(worker, 'known_prior_wake() { pin "$1"', f'v28_wake_sha={digest(old["wake-entry"])}\nv28_conf_sha={digest(old["capture.conf"])}\nknown_prior_wake() {{ pin "$1" "$v28_wake_sha" || pin "$1"')
    worker = replace_once(worker, "matching_prior_service() {\n", """matching_prior_service() {
    if pin "$ENTRY" "$v28_wake_sha"; then
        pin "$DROP/67-nxl-eye-publication.conf" "$v28_conf_sha"
        return
    fi
    absent "$DROP/67-nxl-eye-publication.conf" || return 1
""")
    worker = replace_once(worker, '67-nxl-eye-publication.conf) pin "$conf" "$new_conf_sha"', '67-nxl-eye-publication.conf) pin "$conf" "$v28_conf_sha" || fail FOREIGN_PUBLICATION_OVERRIDE;;\n            68-nxl-full040.conf) pin "$conf" "$new_conf_sha"')
    worker = replace_once(worker, 'NXL_FAST_START_EYE_AF_PUBLICATION28\\npolicy=', 'NXL_FULL040_EYE28\\npolicy=')
    worker = replace_once(worker, 'if [ "$policy" = CHECK ]; then', """mod=$task_root/opt/usr/nx-lut-lite
    actual_manifest=$("$OPS" sha256 "$mod/state/module.manifest") || fail FULL_MODULE_INSTALL_REQUIRED
    case "$actual_manifest" in %s|%s) ;; *) fail FULL_MODULE_INSTALL_REQUIRED;; esac
    regular "$mod/state/module.manifest" &&
        regular "$mod/state/demo51-installed" &&
        grep -qx 'install_revision=release-0.4.0-rc6.14.12-log117' "$mod/state/demo51-installed" ||
        fail FULL_MODULE_INSTALL_REQUIRED
    while read -r wanted file extra; do
        [ -z "$extra" ] && pin "$mod/$file" "$wanted" || fail FULL_MODULE_FILE_MISMATCH
    done < "$mod/state/module.manifest"
    absent "$mod/state/disabled" || fail FULL_MODULE_DISABLED
    printf 'full_module_manifest=%%s\\n' "$actual_manifest"
if [ "$policy" = CHECK ]; then""" % (MODULE, portable_manifest))
    worker = replace_once(worker, 'for leaf in launch.sh readout dispatch.sh;', 'for leaf in launch.sh readout dispatch.sh full-menu.sh;')
    worker = replace_once(worker, 'stock_sleep=$(readlink', f'pin "$PKG/full-menu.sh" {digest(data["full-menu.sh"])} || fail FULL_MENU_IDENTITY\nstock_sleep=$(readlink')
    worker = replace_once(worker, 'if [ "$policy" = INSPECT ]; then\n', '''if [ "$policy" = INSPECT ]; then
    if regular "$STATE/menu-status"; then cp "$STATE/menu-status" "$REPORT/full-menu.txt" || fail MENU_RECEIPT_COPY; fi
''')
    for name in ("launch.sh", "readout", "wake-entry", "dispatch.sh", "loader-observe.sh", "capture.conf"):
        if data[name] != old[name]:
            old_hash = digest(old[name])
            # Keep the independently recognized v2.8 predecessor identities.
            worker = worker.replace(old_hash, digest(data[name]))
    worker = worker.replace("v28_conf_sha=" + digest(data["capture.conf"]), "v28_conf_sha=" + digest(old["capture.conf"]))
    worker = worker.replace("v28_wake_sha=" + digest(data["wake-entry"]), "v28_wake_sha=" + digest(old["wake-entry"]))
    data["worker.sh"] = worker.encode()
    data["activation/install.sh"] = replace_once(data["activation/install.sh"].decode(), digest(old["worker.sh"]), digest(data["worker.sh"])).encode()
    return data


def assemble(full, eye, out, *, stock_reader=None, image_partitions=None):
    if digest(full.read_bytes()) != FULL_ZIP:
        raise ValueError("Full module ZIP is not the hardware-tested version")
    if digest(eye.read_bytes()) != (HERE / "eye-package.sha256").read_text().split()[0]:
        raise ValueError("Public eye release does not match")
    out.mkdir(parents=True, exist_ok=False)
    with zipfile.ZipFile(full) as f:
        if f.testzip() or digest(f.read("nx-lut-demo/module.manifest")) != MODULE:
            raise ValueError("Module manifest or CRC mismatch")
        module = module_files(f)
        for name, value in module.items():
            put(out, "module/" + name, value)
        for name, value in active_sources(f,module).items():
            put(out, "source/work/r10k-ep-flow/" + name,value)
    with zipfile.ZipFile(eye) as f:
        if f.testzip():
            raise ValueError("Eye ZIP CRC mismatch")
        data = combined_eye(f,digest(module[MANIFEST]))
        for name, value in data.items():
            put(out, NEW + "/" + name, value)
        # The checkout keeps the installer in fast-start. An extracted release
        # has the same installer at its root and can also be used to rebuild.
        host_tools = HERE / "fast-start" if (HERE / "fast-start").is_dir() else HERE
        for name in ("stage-postflash.py", "rebuild_evidence.py", "simple_wake.py", "simple-wake-body.sh"):
            put(out, name, (host_tools / name).read_bytes())
        pins = {NEW + "/" + n: digest(v) for n,v in sorted(data.items())}
        put(out, "checksums.json", (json.dumps(pins, indent=2) + "\n").encode())
        for name in f.namelist():
            if name.startswith(("source/", "licenses/")) or name in ("INSTALL.md", "NOTICE.md"):
                put(out, "eye-v2.8/" + name, f.read(name))
    for name in ("README.md", "compose.py", "verify.py", "resume_profile.py", "full-menu.sh", "eye-package.sha256", "gray-calibration.json", "validation.json"):
        put(out, name, (HERE / name).read_bytes())
    for name in ("capture-profile.conf", "verify-before-capture.sh", "protected_writers.py"):
        put(out, "capture-check/" + name, (HERE / "capture-check" / name).read_bytes())
    spec = spec_from_file_location("nx500_resume_profile", HERE / "resume_profile.py")
    profile = module_from_spec(spec)
    spec.loader.exec_module(profile)
    for name in (profile.SERVICE, "resume-after-stock.sh"):
        put(out, "resume-profile/" + name, (HERE / "resume-profile" / name).read_bytes())
    if image_partitions:
        guarded = profile.compose_images(HERE, *image_partitions, out / "image-partitions")
        put(out, "resume-profile/rootfs.tar", guarded)
    else:
        filename = "rootfs.tar" if stock_reader else "services.tar"
        put(out, "resume-profile/" + filename, profile.build(HERE, stock_reader=stock_reader))


def archive(folder, output):
    with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zip_out:
        for path in sorted(folder.rglob("*")):
            if path.is_file():
                name = path.relative_to(folder).as_posix()
                if name.startswith("image-partitions/"):
                    # The owner's rebuilt partitions can include personal device records.
                    continue
                data = path.read_bytes()
                info = zipfile.ZipInfo(name, (2026,11,24,0,0,0))
                info.create_system = 3
                executable = data.startswith((b"#!",b"\x7fELF"))
                info.external_attr = (0o100755 if executable else 0o100644) << 16
                zip_out.writestr(info,data,compress_type=zipfile.ZIP_DEFLATED,compresslevel=6)
    with zipfile.ZipFile(output) as check:
        if check.testzip() or len(check.namelist()) != len(set(check.namelist())):
            raise ValueError("Final archive failed verification")
        for name in check.namelist():
            if check.read(name) != (folder/name).read_bytes():
                raise ValueError(name)
    output.with_suffix(".sha256").write_bytes((digest(output.read_bytes())+"  "+output.name+"\n").encode())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("full", "eye", "out"):
        parser.add_argument(key, type=Path)
    parser.add_argument("--zip", type=Path, help="Write a reproducible release ZIP and SHA-256 file")
    image = parser.add_mutually_exclusive_group()
    image.add_argument("--stock-root", type=Path, help="Use the owner's original NX500 1.12 scripts for the protected image profile")
    image.add_argument("--image-partitions", type=Path, nargs=2, metavar=("PLATFORM_TAR", "OPT_USR_TAR"),
                       help="Merge the registered recovery service and safeguards into two existing 0.4.0 filesystem trees")
    args = parser.parse_args()
    reader = (lambda filename: (args.stock_root / filename.lstrip("/")).read_bytes()) if args.stock_root else None
    assemble(args.full, args.eye, args.out, stock_reader=reader, image_partitions=args.image_partitions)
    if args.zip:
        archive(args.out,args.zip)

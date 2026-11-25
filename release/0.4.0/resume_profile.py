"""Build the combined module's Linux service layout before an image is captured."""

from hashlib import sha256
import copy
import io
from pathlib import Path, PurePosixPath
import tarfile
import tempfile


SERVICE = "nxl-full040-resume.service"
EVENT = "hib_post.service"
SYSTEM = "etc/systemd/system/"
UNIT = SYSTEM + SERVICE
LINK = SYSTEM + EVENT + ".wants/" + SERVICE
HELPER = "opt/usr/nxl-full040/resume-after-stock.sh"
CHECKER = "usr/local/lib/nxl-full040/verify-before-capture.sh"
PRECHECK = SYSTEM + "hibernation.service.d/75-nxl-full040-prewrite.conf"
UNIT_SHA = "17f453e352758641606b51057955ea7b8837345c830fff443852b2b3a2a62004"
HELPER_SHA = "f014691f33bae4c1e84fbf8e3f90d48007d40d426503e3e30c4dba96ce13da21"


def pinned(path, expected):
    data = path.read_bytes()
    if sha256(data).hexdigest() != expected:
        raise ValueError(f"Unexpected previously installed resume component: {path.name}")
    return data


def build(source, *, stock_reader=None):
    source = Path(source)
    components = source / "resume-profile"
    checks = source / "capture-check"
    files = {
        UNIT: (0o644, pinned(components / SERVICE, UNIT_SHA)),
        HELPER: (0o755, pinned(components / "resume-after-stock.sh", HELPER_SHA)),
    }
    if stock_reader is not None:
        # Use only the owner's known NX500 1.12 sources. This output is for the
        # image producer and contains its guarded OEM scripts as well.
        from importlib.util import module_from_spec, spec_from_file_location
        spec = spec_from_file_location("nx500_protected_writers", checks / "protected_writers.py")
        writers = module_from_spec(spec)
        spec.loader.exec_module(writers)
        files[CHECKER] = (0o755, (checks / "verify-before-capture.sh").read_bytes())
        files[PRECHECK] = (0o644, (checks / "capture-profile.conf").read_bytes())
        for name in (writers.STOCK_HEADER, writers.FACTORY_REQUEST):
            files[name.lstrip("/")] = (0o755, writers.protected_source(name, stock_reader(name)))
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w", format=tarfile.USTAR_FORMAT) as archive:
        for name, (mode, data) in sorted(files.items()):
            member = tarfile.TarInfo(name)
            member.mode = mode
            member.size = len(data)
            archive.addfile(member, io.BytesIO(data))
        # Preserve the real Linux link even if the release is built on Windows.
        member = tarfile.TarInfo(LINK)
        member.type = tarfile.SYMTYPE
        member.mode = 0o777
        member.linkname = "/" + UNIT
        archive.addfile(member)
    return output.getvalue()


# Existing components required by the packaged helper on their respective filesystems.
PLATFORM_FILES = {
    "usr/lib/systemd/system/hib_post.path": "59c9006b53597a0f8ce17fe3068e390fdc00472c7529160f15ddeb3c77530027",
    "usr/lib/systemd/system/hib_post.service": "9fc3e5aa13b282b63a68c45bb7277497b61195aac0a8c585dd9d189553cfe4f8",
    "usr/lib/systemd/system/hibernation.service": "6d89c8788aca583164d63f055948809901adbf601626f36bbce9a0f9a4d73636",
    "usr/local/lib/nxl-full040/readout": "bded257edb584f914f0dc2a0d9ea5f84949e2cc89b19ce31d2975ba5e9342bc0",
    "usr/local/lib/nxl-full040/full-menu.sh": "0655ae8a221a71e8a684de78a9df12c59bc7ae301b090bb1c145e95fa53563c4",
    "usr/local/lib/nxl-full040/nxl-eye-af.so": "e167e84a6be88b2fc21da13a5952a923ec7a032cb37612a11e047e60dc6c2e6e",
}
USER_FILES = {"nxl-full040/fileops.arm": "c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d",
              "nx-lut-lite/process.sh": "cf0f12a1b5eba46ae9ec46c2d3b98ff16aed93582d29d378e157ea5d32589867"}


def index(archive):
    result = {}
    for item in archive:
        path = PurePosixPath(item.name)
        if path.is_absolute() or ".." in path.parts or "\\" in item.name or path.as_posix() in result:
            raise ValueError(f"Ambiguous input image entry: {item.name}")
        result[path.as_posix()] = item
    return result


def content(archive, members, name):
    item = members.get(name)
    if item is None or not item.isreg():
        raise ValueError(f"Missing regular image file: {name}")
    return archive.extractfile(item).read()


def update_partition(source, members, destination, expected, changes, replaceable):
    # Only the two pinned OEM writers may replace different regular files.
    for name, pin in expected.items():
        if sha256(content(source, members, name)).hexdigest() != pin:
            raise ValueError(f"Installed component differs: {name}")
    for name, (new, data) in changes.items():
        for parent in PurePosixPath(name).parents:
            old_parent = members.get(parent.as_posix())
            if old_parent is not None and not old_parent.isdir():
                raise ValueError(f"Non-directory image ancestor: {parent}")
        old = members.get(name)
        if old is None:
            continue
        if new.issym() and old.issym() and old.linkname == new.linkname:
            continue
        if old.isreg() and new.isreg():
            previous = content(source, members, name)
            if previous == data or sha256(previous).hexdigest() == replaceable.get(name):
                continue
        raise ValueError(f"Foreign entry at {name}")
    with tarfile.open(destination, "x", format=tarfile.PAX_FORMAT) as out:
        for name, item in members.items():
            if name not in changes:
                out.addfile(item, source.extractfile(item) if item.isreg() else None)
        present = set(members)
        for name, (item, data) in sorted(changes.items()):
            for path in reversed(PurePosixPath(name).parents):
                parent = path.as_posix()
                if parent != "." and parent not in present:
                    folder = tarfile.TarInfo(parent)
                    folder.type, folder.mode = tarfile.DIRTYPE, 0o755
                    out.addfile(folder)
                    present.add(parent)
            if name in replaceable:
                # Preserve the OEM writer's ownership, timestamps and extended
                # attributes, including filesystem security labels if present.
                prev = copy.copy(members[name])
                prev.pax_headers = dict(prev.pax_headers)
                if prev.sparse is not None:
                    raise ValueError(f"Unexpected sparse writer: {name}")
                prev.size = len(data)
                if "size" in prev.pax_headers:
                    prev.pax_headers["size"] = str(prev.size)
                item = prev
            out.addfile(item, io.BytesIO(data) if item.isreg() else None)
    with tarfile.open(destination) as check:
        out_members = index(check)
        for name, (item, data) in changes.items():
            actual = out_members[name]
            expected = members[name] if name in replaceable else item
            if ((expected.type, expected.mode, expected.linkname) != (actual.type, actual.mode, actual.linkname)
                    or actual.isreg() and content(check, out_members, name) != data):
                raise ValueError(f"Image readback failed: {name}")


def compose_images(source, platform, user, destination):
    """Produce two new filesystem archives for the image maker. Inputs stay read-only."""
    destination = Path(destination)
    if destination.exists() or destination.is_symlink():
        raise FileExistsError(destination)
    with tarfile.open(platform) as root, tarfile.open(user) as opt:
        roots, users = index(root), index(opt)
        def stock(name):
            return content(root, roots, name.lstrip("/"))
        data = build(source, stock_reader=stock)
        root_changes, user_changes = {}, {}
        with tarfile.open(fileobj=io.BytesIO(data)) as profile:
            for entry in profile:
                name = entry.name
                target = root_changes
                if name.startswith("opt/usr/"):
                    target = user_changes
                    name = name[len("opt/usr/"):]
                original = profile.extractfile(entry).read() if entry.isreg() else None
                entry.name = name
                target[name] = (entry, original)
        writer_pins = {name: sha256(stock("/"+name)).hexdigest() for name in root_changes
                       if name in ("usr/bin/hibernation.sh", "usr/bin/make_snapshot.sh")}
        # Both independent filesystem images must pass before either is published.
        with tempfile.TemporaryDirectory(dir=destination.parent) as work:
            new = Path(work) / "partitions"
            new.mkdir()
            update_partition(root, roots, new / "platform.tar", PLATFORM_FILES, root_changes, writer_pins)
            update_partition(opt, users, new / "opt-usr.tar", USER_FILES, user_changes, {})
            new.rename(destination)
    return data

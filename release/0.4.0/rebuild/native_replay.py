"""Exercise the new unit graph and writer guards under Samsung's ARM systemd 208."""
import argparse
from hashlib import sha256
import io
import json
from pathlib import Path
import stat
import sys
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent
RELEASE = HERE.parents[3]
sys.path.insert(0, str(RELEASE / "snapshot-graph/native-test"))
import dispatch as native


def profile(bundle):
    pkg = bundle / "nxl-full040-rebuild1"
    files = {"etc/systemd/system/nxl-full040-resume.service": (0o644, (pkg / "nxl-full040-resume.service").read_bytes()),
             "etc/systemd/system/hibernation.service.d/75-nxl-full040-prewrite.conf": (0o644, (pkg / "capture-profile.conf").read_bytes())}
    for name in ("wake.sh", "fileops.arm", "full-menu.sh", "nxl-eye-af.so", "launch.sh", "verify-before-capture.sh"):
        files["usr/local/lib/nxl-full040/" + name] = (0o755, (pkg / name).read_bytes())
    files["usr/local/lib/nxl-full040/capture-origin"] = (0o600, b"11111111-1111-4111-8111-111111111111\nEYE\nFAST_START_EYE_AF_ROOTLIB_100_300\n")
    for name in ("hibernation.sh", "make_snapshot.sh"):
        files["usr/bin/" + name] = (0o755, (pkg / name).read_bytes())
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode='w') as archive:
        for name, (mode, data) in sorted(files.items()):
            entry = tarfile.TarInfo(name)
            entry.mode, entry.size = mode, len(data)
            archive.addfile(entry, io.BytesIO(data))
        entry = tarfile.TarInfo("etc/systemd/system/hib_post.service.wants/nxl-full040-resume.service")
        entry.type, entry.mode, entry.linkname = tarfile.SYMTYPE, 0o777, "/etc/systemd/system/nxl-full040-resume.service"
        archive.addfile(entry)
    return output.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    cache = Path(tempfile.gettempdir()) / "nx500-stock-unit-test-0924"
    data = profile(args.bundle)
    profile_file = args.report.with_suffix('.tar')
    profile_file.write_bytes(data)
    old_guest = native.guest
    def guest(data, binary, case):
        disk = old_guest(data, binary, case)
        if case == 'missing-helper':
            disk.entries.pop('usr/local/lib/nxl-full040/wake.sh')
        if case == 'capture-old-master':
            disk.add('etc/systemd/system/multi-user.target.wants/nx-on-wake.service', stat.S_IFLNK | 0o777,
                     b'/etc/systemd/system/nx-on-wake.service')
        return disk
    native.guest = guest
    native.run(profile_file, cache, args.report, ["old-graph", "stale-manager", "combined", "missing-helper",
        "capture-old-graph", "capture-stale-manager", 'capture-old-master', "capture-combined"])
    report = json.loads(args.report.read_text('utf-8'))
    report['interface'] = 'full040-unified-1'
    report['scope'] = ('Original ARM systemd and real Linux symlinks, new production service and capture checker. '
                       'Image writes are RAM markers. Runtime camera and AF acceptance are covered by PC fixtures and still require the camera.')
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()

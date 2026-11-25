"""Read-only source transformation for an image builder's protected rebuild profile.

Accepts only the audited NX500 1.12 originals. Returns source in memory and
does not install a profile, start a capture or access a device.
"""

from hashlib import sha256


STOCK_HEADER = "/usr/bin/hibernation.sh"
FACTORY_REQUEST = "/usr/bin/make_snapshot.sh"
HEADER_WRITE = b"/bin/dd if=/dev/zero of=$SNAPSHOT_DEV bs=512 count=1\n"
HEADER_GUARD = b"/bin/sh /usr/local/lib/nxl-full040/verify-before-capture.sh --at-image-write || exit 1\n"
FACTORY_BRANCH = b"if [ -e /etc/snapshot ]; then\n"
FACTORY_DENIAL = (b"        echo 'Existing snapshot protected: factory erase would precede a new capture boot.' >&2\n"
                  b"        exit 1\n")
PIN = {
    STOCK_HEADER: "dd3687d3231ec32bbdcaaf3aac3015842f4ec703269a5eef71213e3e75447361",
    FACTORY_REQUEST: "08df6e37016b022944f4d928edf6bb95477327d705001d1af75ad367d198b8c1",
}


def protected_source(name, original):
    if name not in PIN:
        raise ValueError("Unrecognized stock snapshot writer")
    if name == FACTORY_REQUEST:
        anchor, replacement = FACTORY_BRANCH, FACTORY_BRANCH + FACTORY_DENIAL
    else:
        anchor, replacement = HEADER_WRITE, HEADER_GUARD + HEADER_WRITE
    if sha256(original).hexdigest() != PIN[name]:
        if original.count(replacement) == 1 and sha256(original.replace(replacement, anchor)).hexdigest() == PIN[name]:
            return original
        raise ValueError("Unrecognized stock snapshot writer")
    if original.count(anchor) != 1:
        raise ValueError("Expected a unique stock write boundary")
    return original.replace(anchor, replacement)

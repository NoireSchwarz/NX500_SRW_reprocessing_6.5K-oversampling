"""Samsung Log 2025 reference, explicit RGB/luma sample ranges, and CUBE IO.

Verified against the original Samsung white paper (2025), pages 5-7, and
Samsung's v1.0 1D decoder. This module does not establish a camera profile.
Range conversion is for RGB or luma, NEVER the differently centered chroma.
No camera, encoder, firmware, or SD operations are performed here.
"""
from dataclasses import dataclass
from pathlib import Path
import hashlib
import numpy as np

X0, XT, YT = -.05, .01, .206561909
A1, B1, C1 = .258984868, .0003645, .720504856
A2, B2, C2 = -.20942, .016904, -.24597
D65 = (.3127, .3290)
PRIMARIES = {
    'BT709': ((.64, .33), (.30, .60), (.15, .06)),
    'BT2020': ((.708, .292), (.170, .797), (.131, .046)),
}
OFFICIAL_SHA256 = {
    'Samsung+Log+White+Paper_En.pdf': 'a85e7723bcdfa31b32e727c1f0a4f4053885d2dea3f3fa10148bcc30ce71065f',
    'Samsung+Log+to+Linear+1DLUT_v1.0.cube': '526b6d54824780ca3489ea9eb6fb126b794bba416c7b570f705f8377b90c5030',
    'Samsung+Log+to+Rec709+3DLUT_v1.0.cube': '9a77cba1db1eebf39d324a74663c221a8bb01ef501f84c3476134187ff2cb696',
    'SamsungLog_to_CineLUT_Rec709_v1.0.zip': 'fb47cd15dd66fbbb07a1c57b6cb12460775998aaf9680c0475d50969faa418c7',
}


def finite(value):
    a = np.asarray(value, dtype=np.float64)
    if not np.all(np.isfinite(a)):
        raise ValueError('nonfinite input')
    return a


def encode(scene):
    """Published function, including its rounded seams, without range stretching."""
    x = finite(scene)
    y = np.zeros_like(x)
    high = x >= XT
    low = (x >= X0) & ~high
    y[high] = A1 * np.log10(x[high] + B1) + C1
    y[low] = A2 * np.log10(-x[low] + B2) + C2
    return y


def decode(signal):
    y = finite(signal)
    x = np.full_like(y, X0)
    high = y >= YT
    low = (y >= 0) & ~high
    x[high] = 10 ** ((y[high] - C1) / A1) - B1
    x[low] = -10 ** ((y[low] - C2) / A2) + B2
    if not np.all(np.isfinite(x)):
        raise ValueError('decoded signal overflow')
    return x


def sample_limits(bits, sample_range):
    if bits not in (8, 10):
        raise ValueError('only verified 8-bit and 10-bit sample ranges are supported')
    if sample_range == 'full':
        return 0, (1 << bits) - 1
    if sample_range == 'legal':
        scale = 1 << (bits - 8)
        return 16 * scale, 235 * scale
    raise ValueError('range must be explicitly full or legal')


def quantize(signal, *, bits, sample_range):
    """Nearest RGB/luma integer sample; precision remains that of bits."""
    lo, hi = sample_limits(bits, sample_range)
    return np.floor(lo + np.clip(finite(signal), 0, 1) * (hi-lo) + .5).astype(np.uint16)


def normalize(samples, *, bits, sample_range):
    """RGB/luma samples to normalized signal, preserving legal foot/headroom."""
    lo, hi = sample_limits(bits, sample_range)
    a = finite(samples)
    if np.any(a != np.floor(a)) or np.any(a < 0) or np.any(a > (1 << bits)-1):
        raise ValueError('invalid integer samples for bit depth')
    return (a-lo) / (hi-lo)


def convert_samples(samples, *, source_bits, source_range, target_bits, target_range):
    return quantize(normalize(samples, bits=source_bits, sample_range=source_range),
                    bits=target_bits, sample_range=target_range)


def rgb_to_xyz_matrix(primaries):
    p = np.asarray(PRIMARIES[primaries], dtype=float)
    basis = np.stack((p[:, 0]/p[:, 1], np.ones(3),
                      (1-p[:, 0]-p[:, 1])/p[:, 1]))
    xw, yw = D65
    white = np.array((xw/yw, 1, (1-xw-yw)/yw))
    return basis @ np.diag(np.linalg.solve(basis, white))


def primary_matrix(source, target):
    return np.linalg.solve(rgb_to_xyz_matrix(target), rgb_to_xyz_matrix(source))


def encode_linear_rgb(rgb, *, source_primaries):
    """Scene-linear RGB (18% grey=.18) -> BT.2020 -> published Samsung Log.

    source_primaries MUST describe real calibrated linear input. This is not
    suitable for arbitrary factory gamma output or uncalibrated sensor RGB.
    """
    rgb = finite(rgb)
    if rgb.ndim == 0 or rgb.shape[-1] != 3:
        raise ValueError('last RGB dimension must have length three')
    linear2020 = rgb @ primary_matrix(source_primaries, 'BT2020').T
    return encode(linear2020)


@dataclass
class Cube:
    size: int
    dimensions: int
    domain_min: np.ndarray
    domain_max: np.ndarray
    data: np.ndarray


def read_cube(path):
    rows, headers = [], {}
    for raw in Path(path).read_text('utf-8-sig').splitlines():
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        key, *values = line.split()
        if key == 'TITLE':
            continue
        if key.startswith('LUT_') or key.startswith('DOMAIN_'):
            if key in headers:
                raise ValueError('duplicate CUBE header')
            headers[key] = values
        else:
            rows.append([float(v) for v in [key, *values]])
    sizes = [k for k in ('LUT_1D_SIZE', 'LUT_3D_SIZE') if k in headers]
    if len(sizes) != 1:
        raise ValueError('expected one independent 1D or 3D table')
    allowed = {sizes[0], 'DOMAIN_MIN', 'DOMAIN_MAX'}
    if set(headers) - allowed:
        raise ValueError('unsupported CUBE header')
    dim = 1 if sizes[0] == 'LUT_1D_SIZE' else 3
    n = int(headers[sizes[0]][0])
    data = finite(rows)
    lo = finite(headers.get('DOMAIN_MIN', [0, 0, 0]))
    hi = finite(headers.get('DOMAIN_MAX', [1, 1, 1]))
    if n < 2 or data.shape != (n**dim, 3) or lo.shape != (3,) or hi.shape != (3,) or np.any(hi <= lo):
        raise ValueError('invalid CUBE geometry')
    return Cube(n, dim, lo, hi, data)


def sample_cube(cube, rgb):
    """1D linear / 3D trilinear interpolation; CUBE data order is red fastest."""
    rgb = finite(rgb)
    if rgb.ndim == 0 or rgb.shape[-1] != 3:
        raise ValueError('expected RGB vectors')
    shape = rgb.shape
    p = np.clip((rgb.reshape(-1, 3)-cube.domain_min)/(cube.domain_max-cube.domain_min), 0, 1)*(cube.size-1)
    i = np.minimum(np.floor(p).astype(int), cube.size-2)
    f = p-i
    out = np.zeros_like(p)
    if cube.dimensions == 1:
        for c in range(3):
            out[:, c] = cube.data[i[:, c], c]*(1-f[:, c]) + cube.data[i[:, c]+1, c]*f[:, c]
    else:
        table = cube.data.reshape(cube.size, cube.size, cube.size, 3)
        for dz in (0, 1):
            for dy in (0, 1):
                for dx in (0, 1):
                    steps = np.array([dx, dy, dz])
                    w = np.prod(np.where(steps, f, 1-f), axis=1)
                    j = i+steps
                    out += table[j[:, 2], j[:, 1], j[:, 0]]*w[:, None]
    return out.reshape(shape)


def verify_official_files(directory):
    out = []
    for name, expected in OFFICIAL_SHA256.items():
        path = Path(directory)/name
        data = path.read_bytes()
        actual = hashlib.sha256(data).hexdigest()
        if actual != expected:
            raise ValueError('official source changed: '+name)
        out.append({'file': str(path), 'bytes': len(data), 'sha256': actual})
    return out

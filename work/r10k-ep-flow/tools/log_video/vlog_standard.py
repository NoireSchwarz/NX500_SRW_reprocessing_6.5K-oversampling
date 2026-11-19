"""Panasonic reference encoding and explicit output-range interpretation.

These functions define a working colour space, not the NX500 sensor primaries,
physical gray reflectance, or dynamic range. No camera operations occur here.
Source: Panasonic V-Log/V-Gamut Reference Manual Rev.1.0, pp.3-6.
"""
import numpy as np

V_GAMUT_XY = ((.730, .280), (.165, .840), (.100, -.030))
BT709_XY = ((.64, .33), (.30, .60), (.15, .06))
D65 = (.3127, .3290)
OUTPUT_GAIN = 1.0408323021957888
OUTPUT_OFFSET8 = -.8793405230977542
WORKING_GAIN = 2.5203589432187403
PROFILE = 0x31474c56  # VLG1, distinct from all historical Samsung profiles.

def finite(value):
    a = np.asarray(value, dtype=np.float64)
    if not np.all(np.isfinite(a)):
        raise ValueError('nonfinite input')
    return a

def encode(scene):
    x = finite(scene)
    y = np.empty_like(x)
    low = x < .01
    y[low] = 5.6*x[low]+.125
    y[~low] = .241514*np.log10(x[~low]+.00873)+.598206
    return np.clip(y, 0, 1)

def decode(signal):
    y = finite(signal)
    x = np.empty_like(y)
    low = y < .181
    x[low] = (y[low]-.125)/5.6
    x[~low] = 10**((y[~low]-.598206)/.241514)-.00873
    return x

def rgb_xyz(xy):
    p = finite(xy)
    basis = np.stack((p[:,0]/p[:,1], np.ones(3), (1-p[:,0]-p[:,1])/p[:,1]))
    xw,yw = D65
    return basis @ np.diag(np.linalg.solve(basis, [xw/yw,1,(1-xw-yw)/yw]))

def primary_matrix():
    return np.linalg.solve(rgb_xyz(V_GAMUT_XY), rgb_xyz(BT709_XY))

def matrix_q20():
    m = np.floor(primary_matrix()*1048576+.5).astype(np.int64)
    # Preserve the neutral axis exactly after integer rounding.
    for row in m:
        row[np.argmax(row)] += 1048576-int(row.sum())
    return m

def legal709_to_full(rgb):
    """Panasonic official LUT output is 10-bit-normalized legal RGB.

    Do not apply this operation to differently centered YUV chroma samples.
    """
    return (finite(rgb)*1023-64)/876

def input_to_working(scene, gain=WORKING_GAIN):
    """Explicit NX500 working exposure, not certified scene reflectance."""
    rgb = finite(scene)
    if rgb.shape[-1] != 3:
        raise ValueError('expected RGB vectors')
    return encode((rgb @ primary_matrix().T)*gain)

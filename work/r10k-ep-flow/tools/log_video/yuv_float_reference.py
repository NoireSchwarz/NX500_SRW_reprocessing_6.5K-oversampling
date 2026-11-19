"""PC-only float sampling for tagged full-range BT.709 8-bit YUV420.

Avoid PyAV14.2 equal-source/destination reformatter shortcut. This function
performs only the YCbCr matrix conversion; no OETF, LUT, gamut or clipping.
Chroma samples use the recorded 2x2 cell (nearest), stated explicitly.
"""
import numpy as np
KR,KB=.2126,.0722

def rgb_from_full709(y,cb,cr):
 y,cb,cr=np.broadcast_arrays(np.asarray(y,float),np.asarray(cb,float)-128,np.asarray(cr,float)-128)
 return np.stack((y+2*(1-KR)*cr,y-2*KB*(1-KB)/(1-KR-KB)*cb-2*KR*(1-KR)/(1-KR-KB)*cr,y+2*(1-KB)*cb),axis=-1)

def sample_frame_full709(frame,yy,xx):
 if frame.format.name not in('yuvj420p','yuv420p')or frame.color_range!=2 or frame.colorspace!=1:
  raise ValueError('Require8-bit full-range taggedBT709 YUV420; no automatic assumptions')
 yy,xx=np.asarray(yy),np.asarray(xx)
 if yy.shape!=xx.shape or np.any(yy<0)or np.any(yy>=frame.height)or np.any(xx<0)or np.any(xx>=frame.width):raise ValueError('Sample outside frame')
 planes=[np.frombuffer(frame.planes[k],np.uint8).reshape(frame.height if k==0 else frame.height//2,-1)for k in range(3)]
 return rgb_from_full709(planes[0][yy,xx],planes[1][yy//2,xx//2],planes[2][yy//2,xx//2])
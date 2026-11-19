"""PC-only full native Gamma table decoder.

Each 216-byte branch contains signed X15[22], signed Y11[22], and
three 21-entry coefficient arrays. Existing 19-point helpers expose only
X[3:] / Y[3:]. No camera binary or schema is changed here.

For observed tables the third coefficient array is zero. The quadratic
model below follows the original builder's Q12 slope, signed13 curvature,
2-bit-step exponent and Q24 curvature scale. It is not a hardware test;
rounding at the ISP boundary remains an uncertainty.
"""
import struct
import numpy as np
def signed(v,bits):
 v=np.asarray(v,dtype=np.int64)&((1<<bits)-1);return np.where(v&(1<<(bits-1)),v-(1<<bits),v)
class NativeGamma:
 def __init__(self,halfwords):
  q=np.asarray(halfwords,dtype=np.int64)
  if q.shape!=(108,):raise ValueError('Expected one216-byte branch')
  self.x=signed(q[:22],15).astype(float);self.y=signed(q[22:44],11).astype(float)
  self.a=q[44:65]/4096.;self.b=signed(q[65:86],13)*np.power(4.,q[65:86]>>13)/2**24;self.c=q[86:107].copy()
  if not np.all(np.diff(self.x)>0)or not np.all(np.diff(self.y)>0):raise ValueError('Nonmonotonic native knots')
  if np.any(self.c):raise ValueError('Nonzero third coefficient requires independent decoding')
  self.endpoint_error=self.a*np.diff(self.x)+self.b*np.diff(self.x)**2-np.diff(self.y)
 @classmethod
 def from_words(cls,words):
  raw=struct.pack('<108I',*words);return [cls(struct.unpack_from('<108H',raw,k*216))for k in range(2)]
 def __call__(self,x):
  xx=np.asarray(x,dtype=float)*16383;i=np.clip(np.searchsorted(self.x,xx,side='right')-1,0,20);dx=xx-self.x[i];return(self.y[i]+self.a[i]*dx+self.b[i]*dx*dx)/1023
 def inverse(self,y):
  yy=np.asarray(y,dtype=float)*1023;valid=(yy>=self.y[0])&(yy<=self.y[-1]);i=np.clip(np.searchsorted(self.y,yy,side='right')-1,0,20);dy=yy-self.y[i];a=self.a[i];b=self.b[i];disc=a*a+4*b*dy
  # Explicit end-domain extrapolation is diagnostic only and remains invalid.
  good=disc>=0;dx=2*dy/(a+np.sqrt(np.maximum(disc,0)));linear=dy/np.maximum(a,1e-12);dx=np.where(good,dx,linear);return(self.x[i]+dx)/16383,valid&good
 def describe(self):return dict(X22=self.x.astype(int).tolist(),Y22=self.y.astype(int).tolist(),A_Q12=self.a.tolist(),B_Q24_decoded=self.b.tolist(),third_coefficients=self.c.tolist(),max_endpoint_error_Y10=float(abs(self.endpoint_error).max()),full_domain_x=[float(self.x[0]/16383),float(self.x[-1]/16383)],native_negative_knots_supported=True,hardware_rounding_not_verified=True)

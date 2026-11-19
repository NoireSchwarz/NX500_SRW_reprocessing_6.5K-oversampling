"""Decode pinned EP route snapshots; read-only values are not event activity."""
import struct
from video_completion import fnv
def decode_ep_routes(data):
 offset=15552
 if len(data)<16384 or data[offset:offset+8]!=b'NXLERTE1':return None
 if struct.unpack_from('<I',data,offset+8)[0]in [2,3,4]:
  from video_ldc_route import decode_ldc_routes
  return decode_ldc_routes(data)
 magic,schema,size,cap,count,overflow,failed=struct.unpack_from('<8s6I',data,offset)
 if (schema,size,cap)!=(1,128,6) or count>cap or overflow>1:raise ValueError('Invalid EP route header')
 if any(data[offset+32+count*128:offset+800]):raise ValueError('Uncommitted EP route tail')
 rows=[];generations=set()
 for i in range(count):
  raw=data[offset+32+i*128:offset+160+i*128];w=struct.unpack('<32I',raw)
  expected=15|(32 if w[6]&128 else 0)|(64 if w[8]&128 else 0)|(128 if w[12] else 0)|(256 if w[12]==0x801cab24 else 0)|(512 if w[15] else 0)
  if w[0]!=i or w[31]!=(fnv(raw[:124])^0x6a31d5c8) or w[5]!=expected or w[18]!=w[19] or w[20]!=w[21] or w[22]not in [4,6,7,8] or w[29] in generations:raise ValueError('Invalid EP route record')
  generations.add(w[29]);rows.append(dict(sequence=i,generation=w[29],elapsed_s=w[1]/1000,driver=hex(w[2]),preset=hex(w[3]),source_state=w[4],coherent=True,timer_schema=w[22],native_masks=[hex(w[6]),hex(w[7])],hardware_enables=[hex(w[8]),hex(w[9])],bank0_callbacks_ports5_to9=[hex(x) for x in w[10:15]],bank1_callback_port7=hex(w[15]),upper_owners_bank0_bank1=[hex(w[16]),hex(w[17])],software_event7_enabled=bool(w[6]&128),hardware_event7_enabled=bool(w[8]&128),callback7_nonzero=bool(w[12]),callback7_is_old_upper_entry=w[12]==0x801cab24,native_sequence=w[18],native_flags=hex(w[20]),earlier_native_IRQ_A_B_DMA=list(w[23:26]),backend=hex(w[26]),old_upper_entry_instruction=hex(w[27]),new_dispatch_entry_instruction=hex(w[28]),read_span_us=w[30]))
 return dict(schema='NXL_EP_ROUTE1',count=count,capacity=cap,overflow=bool(overflow),failed_attempts=failed,records=rows,interpretation='Pinned after2s stable video context, once per generation. Native masks/callbacks/owners and hardware enables were read twice with unchanged IPC header/mode/source. Earlier IRQ totals are from the preceding counter capture. Physical instruction bytes do not prove instruction-cache visibility. Configuration and nonzero callbacks do not prove an event fired, buffer identity, DMA retirement or latency.')

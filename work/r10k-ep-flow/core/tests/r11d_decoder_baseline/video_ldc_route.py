"""Decode schema2/3 route pins; schema2 cannot validate strategy provenance; flags are independently recomputed from raw values."""
import struct
from video_completion import fnv
def decode_ldc_routes(data):
 if struct.unpack_from('<I',data,15560)[0]==4:
  from video_ldc_route4 import decode_ldc_routes4
  return decode_ldc_routes4(data)
 o=15552;magic,s,size,cap,count,overflow,failed=struct.unpack_from('<8s6I',data,o)
 if magic!=b'NXLERTE1'or s not in [2,3]or(size,cap)!=(128,6)or count>cap or overflow>1:raise ValueError('Invalid LDC route header')
 if any(data[o+32+128*count:o+800]):raise ValueError('Uncommitted LDC route tail')
 rows=[];gens=set()
 for i in range(count):
  raw=data[o+32+i*128:o+160+i*128];w=struct.unpack('<32I',raw);f=15
  if w[6]&1024:f|=32
  if w[8]&1024:f|=64
  if w[11]:f|=128
  if w[11]==0x801ca2f0:f|=256
  if w[17]>=0x81006000 and w[17]+16<=0x94000000 and not w[17]&3 and w[13]==w[17]+12:f|=512
  if w[14]==0x80582ba8:f|=1024
  if (w[15],w[27])in [(0x80582c38,0x80582c98),(0x80582d10,0x80582d70),(0x80582da8,0x80582e08)]:f|=2048
  if w[25]==0x80592110 and w[24]in [0x80592268,0x80592328]:f|=4096
  if w[17]>=0x81006000 and w[17]+16<=0x94000000 and not w[17]&3:f|=8192
  if w[16]==3:f|=16384
  if w[0]!=i or w[31]!=(fnv(raw[:124])^0x6a31d5c8)or w[5]!=f or w[18]!=w[19]or w[20]!=w[21]or w[22]not in [4,6,7,8,9]or w[29]in gens:raise ValueError('Invalid LDC route record')
  gens.add(w[29]);required=15|32|64|128|256|512|1024|2048|4096
  rows.append(dict(sequence=i,generation=w[29],elapsed_s=w[1]/1000,driver=hex(w[2]),preset=hex(w[3]),source_state=w[4],flags=hex(w[5]),timer_schema=w[22],native_masks=[hex(w[6]),hex(w[7])],hardware_enables=[hex(w[8]),hex(w[9])],callback0=hex(w[10]),callback10=hex(w[11]),config_owner=hex(w[12]),LDC_owner=hex(w[13]),LDC_primary_type=hex(w[14]),LDC_operation_type=hex(w[15]),LDC_state=w[16],LDC_operation=hex(w[17]),native_sequence=w[18],native_flags=hex(w[20]),backend_pointer=hex(w[23]),backend=hex(w[24]),parent_type=hex(w[25]),earlier_IRQ_A=w[26],LDC_notify_type=hex(w[27]),new_dispatch_entry_instruction=hex(w[28]),read_span_us=w[30],raw_object_fields_match=(f&required)==required,operation_pointer_source=('0x8142efb8'if s==3 else'legacy-parameter-root-0x8142f1e8'),object_binding_valid=s==3 and(f&required)==required))
 return dict(schema='NXL_LDC_ROUTE'+str(s),count=count,capacity=cap,overflow=bool(overflow),failed_attempts=failed,records=rows,interpretation=('Schema3 reads selected strategy at0x8142efb8 (native parent+0xFA0), and matches native owner=strategy+0xC and exact primary/notify types. Schema2 read a scalar parameter root and is retained only as unqualified evidence. Two matching vectors are bracketed by the same timer, mode/source and IPC backend. Sparse types do not prove per-buffer identity or uninterrupted lifetime. Configuration does not prove an event fired.'))

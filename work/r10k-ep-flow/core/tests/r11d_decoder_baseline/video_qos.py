"""Decode R10f read-only bus snapshots in previously unused header padding."""
import struct
ADDRESSES=[0x1ff04d0c,0x1ff04d10,0x20820150,0x208201a0,0x20821110,0x20821210,0x2012f138,0x20810308,0x10400038,0x1040003c,0x10400040,0x10400044,0x10400048,0x1040004c,0x10400050,0x10400058,0x10400028]
def fnv(data):
 h=2166136261
 for x in data:h=((h^x)*16777619)&0xffffffff
 return h
def decode_qos(data):
 ring=data[640:1024]
 if ring[:8]==bytes(8):return None
 if ring[:8]!=b'NXLQOS1\0':raise ValueError('Unknown QoS snapshot extension')
 magic,schema,size,capacity,total,mask,reserved=struct.unpack_from('<8s6I',ring)
 if(schema,size,capacity,reserved)!=(1,108,3,0)or mask&~7:raise ValueError('Invalid QoS snapshot metadata')
 rows=[]
 for phase in range(3):
  if not mask&(1<<phase):continue
  raw=ring[32+108*phase:32+108*(phase+1)];w=struct.unpack('<27I',raw)
  if w[-1]!=(fnv(raw[:-4])^0x4c31ad87)or w[0]>=total or w[3]!=phase:raise ValueError('QoS snapshot integrity failed')
  rows.append(dict(sequence=w[0],elapsed_s=w[1]/1000,status=w[2],phase=['preview','recording','after_stop'][phase],driver_mode=hex(w[4]),scene=hex(w[5]),preset=hex(w[6]),IRQ_handler=hex(w[7]),valid_mask=hex(w[8]),registers={hex(a):hex(w[9+i])if w[8]&(1<<i)else None for i,a in enumerate(ADDRESSES)}))
 return dict(schema='NXL_QOS1',total_captured=total,records=rows,interpretation='Original bus/EP register reads only. Matching repeated reads and unchanged route required for status 1. HEVC bridge 0x2012f138 is deliberately unavailable outside observed successful encoding; never read while its power state is unknown. Registers are not bandwidth counters. No writes or forced QoS/clock changes.')

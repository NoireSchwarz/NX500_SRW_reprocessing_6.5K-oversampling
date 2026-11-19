from pathlib import Path
import json,re,struct,tempfile,zlib
from analyze_init_trace import frames,analyze,audit_file,largest_span
R=Path(__file__).resolve().parents[1];out=R/'validation/r7g'
s=(R/'core/src/nxl-handoff-watch.c').read_text();body=s.split('static const uint32_t ram_addresses[]={',1)[1].split('};',1)[0];addresses=[int(x,16) for x in re.findall(r'0x([0-9a-f]+)u',body)]
keys=[0x8153f3f8,0x81540f38,0x8142de78,0x867555c4,0x86755238,0x867554c4]
def frame(kind,seq,payload):
 data=struct.pack('<%dI'%len(payload),*payload);h=[0x3152584e,kind,seq,1000+seq,0,1001+seq,0,len(data),zlib.crc32(data)];head=struct.pack('<9I',*h);return head+struct.pack('<I',zlib.crc32(head))+data
with tempfile.TemporaryDirectory(dir=out) as temp:
 p=Path(temp);(p/'schema.json').write_text(json.dumps(dict(format='NXINITBROAD1',hot_addresses=addresses,repeat_key_addresses=keys)))
 w=[0]*len(addresses);w[addresses.index(0x8153f3f8)]=7
 repeated=[w[addresses.index(k)] for k in keys]
 a=frame(1,0,[0,0,0,0,*w,*repeated]);b=frame(1,5,[4,0,0,0,*w,*repeated])
 for leaf in ['contexts.bin','allocator.bin','linux-context.bin']:(p/leaf).write_bytes(b'')
 (p/'hot.bin').write_bytes(a+b+b[:11]);result=analyze(p)
 assert result['hot_represented_samples']==6 and result['transport']['hot.bin']['tail_bytes']==11
 assert result['hot_state_timeline'][0]['still_state']==7 and not result['observer_end_seen']
 (p/'hot.bin').write_bytes(a[:-1]+bytes([a[-1]^1])+b);rows,status=frames(p/'hot.bin');assert not rows and status['error']=='INVALID_PAYLOAD_CRC'
 (p/'hot.bin').write_bytes(a+frame(1,5,[0,0,0,0,*w,*repeated]))
 try:analyze(p);raise AssertionError('broken skip chain accepted')
 except ValueError as e:assert 'skipped-sample' in str(e)
 raw=bytearray(45056*16);assert largest_span(raw)==45056*16384
 struct.pack_into('<4I',raw,0,1,8192,0,0);assert largest_span(raw)==(45056-8192)*16384
 struct.pack_into('<I',raw,4,50000);assert largest_span(raw) is None
 audit=audit_file(out/'actual-still-audit-model.bin');assert len(audit)==6 and all(x['transport_valid'] for x in audit)
 assert all([s['count'] for s in x['semaphores']]==[1,1] for x in audit)
summary=dict(format='NX_R7G_TRACE_READER_TEST1',passed=True,hardware_tested=False,
 incomplete_power_loss_tail_preserves_valid_prefix=True,CRC_corruption_stops_decode=True,
 change_suppression_sequence_checked=True,allocator_span_union_verified=True,invalid_allocator_metadata_remains_unknown=True,
 actual_ARM_generated_lifecycle_records_decoded=True,hot_fields=len(addresses),repeat_key_fields=len(keys))
(out/'reader-tests.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

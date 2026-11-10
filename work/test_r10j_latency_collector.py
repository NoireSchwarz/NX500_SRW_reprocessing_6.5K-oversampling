"""Run production ARM sampler with modeled hardware/IRQ phase boundaries."""
from pathlib import Path
import sys,struct,json,hashlib
W=Path(__file__).resolve().parent;sys.path.insert(0,str(W))
import test_r10j_recording_evidence as base
from analyze_video_counters import decode
p=base.p;rows=[]
class Timed(base.Recording):
 def __init__(self,*a,scenario='hardware',**kw):self.scenario=scenario;super().__init__(*a,**kw)
 def process_words(self):
  w=super().process_words();source=0x20002000;w[source+0x138+11*4]=2 if 5<self.sleeps<=55 else 1
  if self.sleeps>55:w[source+0x138+9*4]=0
  return w
 def ram_words(self):
  w=super().ram_words();n=self.sleeps*6;flags=15|(1<<8)|(1<<16)|(1<<24);period=1170000;hw,irq,cb=2600,1120600,1123200
  if self.scenario=='route':hw,irq,cb=2600,392600,1120600
  if self.scenario=='before_launch':hw,irq,cb=390000,1040000,1118000
  if self.scenario=='missing_IRQ':irq=0;flags&=~4
  if self.scenario=='IRQ_error':flags|=128
  t=(0xffff0000+n*period)&0xffffffff
  for i,v in enumerate([0x33544c56,3,n,1,t,0,0,0]):w[0x817dbbe0+4*i]=v
  for seq in range(max(1,n-7),n+1):
   t=(0xffff0000+seq*period)&0xffffffff;values=[seq,t,(t+hw)&0xffffffff,(t+irq)&0xffffffff if flags&4 else 0,(t+cb)&0xffffffff,(t+period)&0xffffffff,flags]
   for i,v in enumerate(values):w[0x817dbc00+(seq&7)*28+4*i]=v
  return w
def trial(scenario,DDR='700000000\n'):
 f=p.base.fixture()
 if DDR is not None:p.base.put(f,'/sys/devices/platform/drime5-ddrfreq/ch_a',DDR)
 m=Timed(f,['--run',p.base.REPORT],scenario=scenario,after=80);out=m.run();assert out['exit']==0,out
 path=f/(p.base.REPORT+'/counts.bin').lstrip('/');s,_=decode(path);trace=s['frame_timing'];stats=trace['statistics'];assert trace['schema']=='NXL_HW_TIMING3'and trace['retained_frames']==474 and trace['missed_frame_sequences']==0
 if scenario=='hardware':assert stats['hardware_to_IRQ_ms']['median']==43 and abs(stats['IRQ_to_callback_ms']['median']-.1)<1e-9
 elif scenario=='route':assert stats['hardware_to_IRQ_ms']['median']==15 and stats['IRQ_to_callback_ms']['median']==28
 elif scenario=='before_launch':assert stats['request_to_hardware_ms']['median']==15
 elif scenario=='missing_IRQ':assert not stats and all('CTRL0-CS0-frame-done-IRQ'in x['missing_events']for x in trace['records'])
 elif scenario=='IRQ_error':assert not stats and all('IRQ-error-bit'in x['problems']for x in trace['records'])
 assert all(x['raw_words'][0]==x['sequence']for x in trace['records'])and not m.hot_sd
 clocks=s['clock_snapshots'];assert len(clocks['records'])==3 and clocks['records'][0]['phase']=='preview-before-recording'
 assert all(x['DDR_readback_status']==(1 if DDR=='700000000\n'else 3)for x in clocks['records'])
 qos=s['qos_snapshots'];assert {x['phase']for x in qos['records']}=={'preview','recording','after_stop'}
 assert all(x['registers']['0x2012f138']is None for x in qos['records']) # no simulated successful encoder in this fixture
 bus=s['bus_snapshots'];assert bus['schema']=='NXL_BUS1'and {x['phase']for x in bus['records']}=={'preview','recording','after_stop'}
 assert all(len(x['registers'])==136 for x in bus['records'])and bus['total_captured']<=4
 bad=bytearray(path.read_bytes());bad[8192+508+32]^=1;(path.parent/'corrupt.bin').write_bytes(bad)
 try:decode(path.parent/'corrupt.bin')
 except ValueError:pass
 else:raise AssertionError('Bad phase sample checksum accepted')
 rows.append(dict(case=scenario,DDR_available=DDR=='700000000\n',statistics=stats,clocks=clocks,QoS=qos,DMA_bus=bus,frames=trace['retained_frames'],no_hot_SD_writes=True,corruption_rejected=True));print('PASS actual ARM hardware/IRQ reader',scenario,flush=True)
for scenario in ['hardware','route','before_launch','missing_IRQ','IRQ_error']:trial(scenario)
trial('hardware',None);trial('hardware','700MHz\n')
for n in ['r10f-field-evidence/NXDEMO51/VIDEO_STAGE_1794250416_431/counts.bin','r10e-field-evidence/NXDEMO51/VIDEO_STAGE_1794246806_432/counts.bin','r10d-r10c-evidence/NXDEMO51/VIDEO_STAGE_1794242226_432/counts.bin','r10c-r10b-evidence/NXDEMO51/VIDEO_STAGE_1794236179_432/counts.bin']:
 s,_=decode(W/n);rows.append(dict(case='real-legacy-'+s['revision'],samples=s['samples'],passed=True))
out=dict(passed=True,cases=rows,binary_sha256=hashlib.sha256(p.base.BIN.read_bytes()).hexdigest(),source_sha256=hashlib.sha256((p.R/'core/src/nxl-video-probe.c').read_bytes()).hexdigest(),hardware_tested=False)
(p.R/'validation/current/latency-collector.json').write_text(json.dumps(out,indent=2)+'\n',encoding='utf8',newline='\n')

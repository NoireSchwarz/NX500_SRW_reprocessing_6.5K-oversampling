"""R11f admission intervention. RTOS durations; Linux publication brackets for cohorts.
No unique DDR attribution or 30p repair is inferred from dropped video frames.
"""
import collections,statistics
MAGIC=0x31474148
NAMES=['A1_NORMAL','B_QUARTER_HEVC','A2_NORMAL']
def avg(a):return statistics.mean(a)if a else None
def desc(a):return dict(n=len(a),mean=avg(a),median=statistics.median(a)if a else None,p95=sorted(a)[min(len(a)-1,int(len(a)*.95))]if a else None)
def whash(w):
 h=2166136261
 for x in w:h=((h^x)*16777619)&0xffffffff
 return h
def decode_record(flags,d,begin,end):
 if flags or begin!=end or d[:2]!=[MAGIC,1]or d[2]not in [1,2,3,4]or d[3]not in [1,2,3,4,5]or d[4]>8 or d[5]>3 or not d[6]or d[14:16]!=[30000,1200]or any(d[125:]):raise ValueError('HEVC A/B/A record header')
 c=d[40:56];s=d[56:104]
 if d[110]not in [0,1]or (d[116]|d[117]<<32)>(d[118]|d[119]<<32)or d[110]and(d[118]|d[119]<<32)>begin:raise ValueError('HEVC independent snapshot bounds')
 if ((d[10]-c[0])&0xffffffff)>=0x80000000 or c[0]&1 or c[1]!=1 or c[2]!=d[6]or c[3]!=d[11]or c[3]>1 or c[-1]!=whash(c[1:15]):raise ValueError('HEVC command commit/hash')
 if s[0]&1 or s[1]not in [0,d[6]]:raise ValueError('HEVC native counter commit/owner')
 if s[1] and(s[2]!=c[4]or s[3]!=c[5]):raise ValueError('HEVC native counter identity')
 for p in range(3):
  if s[20+p]!=s[23+p]+s[26+p]or s[29+p]>s[23+p]:raise ValueError('HEVC admission ledger')
 if s[26]or s[28]:raise ValueError('HEVC drop outside B')
 return dict(hevc_event=d[2],hevc_status=d[3],hevc_reason=d[4],hevc_phase=d[5],capture_cookie=d[6],
  experiment_start_ns=d[104]|d[105]<<32,experiment_finish_ns=d[106]|d[107]<<32,telemetry_ns=d[108]|d[109]<<32,
  unrelated_encoder_calls=d[123],unrelated_encoder_last_this=d[124],
  HAL_read_valid=d[110]==1,HAL_successful_total=d[111],shape=d[112:116],HAL_begin_ns=d[116]|d[117]<<32,HAL_end_ns=d[118]|d[119]<<32,
  mailbox_enabled=bool(d[11]),CPU0_kHz=d[16],governor=d[17],DDR_A_Hz=d[18],DDR_status=d[19],telemetry_age_ms=d[20],
  camera_key=d[24:32],live_identity=d[32:40],native_stats=s,command=c,concurrency=d[13])
def finish_aba(rows,native=None,header=None):
 logs=[r for r in rows if r['kind']==12]
 if not logs or logs[0]['hevc_event']!=1 or sum(r['hevc_event']==1 for r in logs)!=1:raise ValueError('HEVC A/B/A config lifetime')
 last=logs[-1];cookie=logs[0]['capture_cookie'];starts={r['experiment_start_ns']for r in logs if r['experiment_start_ns']}
 if len(starts)>1 or any(r['capture_cookie']!=cookie for r in logs):raise ValueError('HEVC A/B/A capture identity')
 start=next(iter(starts))if starts else None
 if start and any(r['command'][10]-r['command'][9]&0xffffffff!=30000 or r['command'][11]-r['command'][10]&0xffffffff!=30000 or r['command'][12]-r['command'][11]&0xffffffff!=30000 for r in logs if r['experiment_start_ns']):raise ValueError('HEVC schedule')
 completed=last['hevc_event']==3 and last['hevc_status']==3 and start and last['experiment_finish_ns']>=start+90_000_000_000
 if header:
  if cookie!=header[23]or header[12]:raise ValueError('HEVC capture/header mismatch')
  progress=3 if last['hevc_status']==3 else last['hevc_phase']or 1 if start else 0
  if header[20]!=(0xe1000000|last['hevc_status']|(last['hevc_reason']<<8)|(progress<<16)):raise ValueError('HEVC UI status differs from saved evidence')
 ordered=sorted((native or {}).get('records',[]),key=lambda r:(r['generation'],r['sequence_ordinal']))
 cohorts=[]
 for p,name in enumerate(NAMES):
  lo=start+p*30_000_000_000+3_000_000_000 if start else 0;hi=lo+26_500_000_000
  tt=[r for r in logs if lo<=r['begin_ns']<=hi and r['hevc_status']==2 and r['hevc_phase']==p+1]
  ff=[]
  for a,b in zip(ordered,ordered[1:]):
   if a['generation']!=b['generation']or b['sequence_ordinal']!=a['sequence_ordinal']+1:continue
   if not all(x.get('valid_for_bit6_statistics')and x.get('source_state')==2 and x.get('driver_mode')=='0x1717'and x.get('IPC_preset')=='0xe005'for x in [a,b]):continue
   xx=[x.get('callback_publish_linux_bounds')for x in [a,b]]
   if not all(x and x.get('earliest_linux_ns')is not None and lo<=x['earliest_linux_ns']<=x['latest_linux_ns']<=hi for x in xx):continue
   if not 0<a.get('cycle_ms',0)<200 or not 0<a.get('IPC_bit6_to_final_IRQ_ms',0)<200:continue
   ff.append(a)
  mod=collections.defaultdict(list)
  for f in ff:mod[f['sequence']%10].append(f['IPC_bit6_to_final_IRQ_ms'])
  def ledger(r):s=r['native_stats'];return s[20+p],s[23+p],s[26+p],s[29+p]
  x,y=ledger(tt[0])if tt else(0,0,0,0),ledger(tt[-1])if tt else(0,0,0,0);delta=[b-a for a,b in zip(x,y)]
  span=(tt[-1]['begin_ns']-tt[0]['begin_ns'])/1e9 if len(tt)>1 else 0
  clocks_ok=bool(tt and all(r['telemetry_age_ms']<=1500 and r['CPU0_kHz']>0 and r['DDR_A_Hz']>0 and r['DDR_status']==1 and r['governor']>0 for r in tt))
  errors=any(r['concurrency']or any(r['native_stats'][i]for i in [5,6,7,8])for r in tt)
  eligible=bool(completed and len(ff)>=300 and len(tt)>=45 and span>=23 and clocks_ok and not errors and len(mod)==10 and delta[0]>300 and delta[1]>0)
  cohorts.append(dict(phase=p+1,name=name,begin_ns=lo if start else None,end_ns=hi if start else None,recording_cycles=desc([f['cycle_ms']for f in ff]),bit6_to_bit2_ms=desc([f['IPC_bit6_to_final_IRQ_ms']for f in ff]),
   equal_mod10_bit6_to_bit2_ms=avg([avg(mod[k])for k in range(10)])if len(mod)==10 else None,ordinary_nine_phase_cycle_ms=avg([avg([f['cycle_ms']for f in ff if f['sequence']%10==k])for k in range(1,10)])if all(any(f['sequence']%10==k for f in ff)for k in range(1,10))else None,
   observed_seconds=span,seen=delta[0],passed=delta[1],dropped=delta[2],control=delta[3],admitted_frames_per_s=delta[1]/span if span else None,
   pass_fraction=delta[1]/delta[0]if delta[0]else None,CPU0_kHz=dict(collections.Counter(r['CPU0_kHz']for r in tt)),DDR_A_Hz=dict(collections.Counter(r['DDR_A_Hz']for r in tt)),governor=dict(collections.Counter(r['governor']for r in tt)),samples=len(tt),eligible=eligible))
 a,b,c=cohorts
 dominant=lambda h:max(h,key=h.get)if h else None
 clock_match=all(dominant(a[k])==dominant(b[k])==dominant(c[k])not in [None,0]for k in ['CPU0_kHz','DDR_A_Hz','governor'])
 # Admission/s alone is insufficient when upstream accelerates in B. Both ratio
 # and absolute input rate must fall substantially relative to both A phases.
 dose=bool(b['pass_fraction']is not None and .20<=b['pass_fraction']<=.35 and all(x['pass_fraction']is not None and x['pass_fraction']>=.98 for x in [a,c])and b['admitted_frames_per_s']<.45*min(a['admitted_frames_per_s'],c['admitted_frames_per_s']))
 valid=bool(completed and all(x['eligible']for x in cohorts)and clock_match and dose and not(native or {}).get('missed_frame_sequences',1))
 base=avg([x['equal_mod10_bit6_to_bit2_ms']for x in [a,c]])if all(x['equal_mod10_bit6_to_bit2_ms']is not None for x in [a,c])else None
 recovery=bool(base and abs(a['equal_mod10_bit6_to_bit2_ms']-c['equal_mod10_bit6_to_bit2_ms'])<=1.)
 gain=base-b['equal_mod10_bit6_to_bit2_ms']if base and b['equal_mod10_bit6_to_bit2_ms']is not None else None
 verdict='INSUFFICIENT_CAUSAL_EXPERIMENT'
 if valid and recovery:verdict='CANDIDATE_ENCODER_RELATED_UPSTREAM_EFFECT'if gain>=1. else'HEVC_ADMISSION_REDUCTION_NO_MATERIAL_UPSTREAM_GAIN'if abs(gain)<=.5 else'SMALL_OR_MIXED_EFFECT_REQUIRES_ANALYSIS'
 return dict(schema='R11F_HEVC_ADMISSION_ABA1',experiment_completed=bool(completed),meta_events=logs,cohorts=cohorts,clock_match=clock_match,actual_admission_dose_qualified=dose,evidence_qualified=valid,recovery_within_1ms=recovery,bit6_to_bit2_recovery_baseline_ms=base,bit6_to_bit2_reduction_in_B_ms=gain,verdict=verdict,
  DDR_root_uniquely_proven=False,bitrate_test=False,performance_fix=False,frame_dropping_is_diagnostic_only=True,
  qualifications_not_automatically_proven=['Encoded HAL successful completion rate reduction','Fullwidth RAW/output geometry unchanged','Clock histogram tails/QoS unchanged','No camera errors and normal restored MP4 finalization'],
  interpretation='One A/B/A is a causal diagnostic for encoder-related load/credit/scheduling, not a unique DDR measurement. Require independent HAL completion rates and geometry/clock/QoS review before final attribution. Cohorts use full adjacent Linux publication brackets,3s settling/0.5s tail exclusion; all frame durations remain in RTOS clock.')

"""Read-only PC decoder for R9i kind16/17 records; reports cached state only.
Usage: python analyze_video_front.py path/to/INIT_LENS_report output.json
"""
from pathlib import Path
import argparse,collections,json,struct
from compare_selected_photo import frames
def fnv(b):
 h=2166136261
 for x in b:h=((h^x)*16777619)&0xffffffff
 return h
def analyze(report):
 counts=collections.Counter();front=[];sensor=[];windows=[];profiles=[];runtime=[];last={}
 for h,p in frames(Path(report)/'contexts.bin'):
  if h['kind']not in [16,17,18,19,20]:continue
  kind=h['kind'];expected,magic=(980,0x3146564e)if kind==16 else(148,0x3153564e)if kind==17 else(140,0x3147564e)if kind==18 else((len(p) if len(p)in[488,688,696]else 0),0x314f574e)if kind==19 else(128,0x3152564e)
  assert len(p)==expected
  v=struct.unpack('<%dI'%(len(p)//4),p);assert v[0]==magic and (v[1]==1 or(kind==19 and ((v[1]==2 and len(p)==688)or(v[1]==3 and len(p)==696))))and v[-2]==fnv(p[:-8])and v[-1]==(magic^v[2]^v[-2])
  counts[f'kind{kind}_status{v[2]}']+=1
  if v[2]!=1:continue
  if kind==16:
   obj=p[40:40+932];row=dict(object=hex(v[4]),sensor_interface_pointer=hex(struct.unpack_from('<I',obj,0x25c)[0]),fps_request_candidate=struct.unpack_from('<I',obj,0x2ac)[0],video_exposure_time_us=struct.unpack_from('<I',obj,0x288)[0],video_sensitivity_raw=struct.unpack_from('<I',obj,0x28c)[0],video_flags=list(obj[0x290:0x297]),still_config_hex=obj[0x278:0x288].hex(),video_config_hex=obj[0x288:0x298].hex(),native_six_word_config=list(struct.unpack_from('<6I',obj,0x2c4)))
  elif kind==17:
   obj=p[28:28+112];row=dict(object=hex(v[4]),requested_mode=obj[8],applied_mode=obj[9],geometry_candidates=list(struct.unpack_from('<II',obj,0x10)),raw_control_hex=obj.hex())
  elif kind==18:
   assert v[9]==480 and v[10]==22;fields=v[11:33];row=dict(object=hex(v[4]),requested_mode=v[7]&255,applied_mode=(v[7]>>8)&255,cached_window_width=fields[11],cached_window_height=fields[10],window_words=list(fields),expected_register_values={'0x325a':fields[11],'0x324a':fields[11],'0x32ea':fields[10]},sensor_write_observed=False)
  elif kind==19:
   s=v[4:-2];a=s[20:68];b=s[68:116];offsets=[0x14,0x18,0x1c,0x20,0x3c,0x40]
   row=dict(root=hex(s[0]),profile6=hex(s[4]),profile19=hex(s[5]),mapping_literal=hex(s[6]),requested_mode=s[7]&255,applied_mode=(s[7]>>8)&255,profile6_geometry=[a[i//4]for i in offsets],profile19_geometry=[b[i//4]for i in offsets],profile19_RAW_bytes=struct.pack('<I',b[0x50//4]).hex(),movie_work_descriptor=list(s[8:13]),movie_source_descriptor=list(s[13:18]),work_frame_budget=s[18],source_frame_budget=s[19],sensor_readout_or_scaler_pixel_effect_proven=False)
   if v[1]>=2:row.update(power_manager=hex(s[116]),video_power=hex(s[117]),power_operations=hex(s[118]),power_entry=hex(s[121]),UD_power_scene=s[126],UD_HDMI_scene=s[127],native_full_scene_words=list(s[146:156]),native_crop_scene_words=list(s[156:166]))
   if v[1]==3:row.update(IPC_setup_function=hex(s[166]),native_IPC_preset=hex(s[167]),fullwidth_scaler_selected=s[166]==0x801cf038)
  else:
   row=dict(requested_mode=v[4]&255,applied_mode=(v[4]>>8)&255,native_power_scene=v[6]&255,PP_EOF=v[8],PP_WDMA0=v[9],SSIF_SOF=v[10],SSIF_EOF=v[11],clock_selectors=list(v[12:21]),clock_stable=v[12:21]==v[21:30],counters_are_native_RAM_ISR_counts=True,atomic=False)
  if last.get(kind)!=row:
   last[kind]=row.copy();row.update(begin_ns=h['begin_ns'],end_ns=h['end_ns'],sequence=h['sequence']);(front if kind==16 else sensor if kind==17 else windows if kind==18 else profiles if kind==19 else runtime).append(row)
 progress=dict(PP_EOF=False,PP_WDMA0=False)
 for a,b in zip(runtime,runtime[1:]):
  if all(x['requested_mode']==23 and x['applied_mode']==23 and x['native_power_scene']==8 and x['clock_stable']for x in[a,b]):
   for key in progress:
    delta=(b[key]-a[key])&0xffffffff
    if 0<delta<10000:progress[key]=True
 return dict(format='NX_VIDEO_FRONT_PC4',record_counts=dict(counts),frontend_changes=front,sensor_cache_changes=sensor,sensor_window_changes=windows,fullwidth_profile_changes=profiles,video_runtime_changes=runtime,fullwidth_native_ISR_progress=progress,video_file_or_pixel_quality_proven=False,atomic=False,physical_sensor_readout_proven=False,scaler_geometry_proven=False,notes=['Frontend first two video configuration words are exposure time and sensitivity, not width/height or video-mode enum.','Sensor mode bytes and window parameters are cached driver state, not independent sensor register or raw frame proof.','Associate changes with recorder marks and actual video files before identifying readout behavior.','Kind19 schema2 adds the guarded native UD power operation entry and unmodified scene rows. Kind20 records stable-bracketed clock selectors and non-clearing native PP/SSIF ISR counters; advancing counts are frontend event evidence, not pixel-quality or a saved movie proof.'])
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('report',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(analyze(a.report),ensure_ascii=False,indent=2)+'\n',encoding='utf8')

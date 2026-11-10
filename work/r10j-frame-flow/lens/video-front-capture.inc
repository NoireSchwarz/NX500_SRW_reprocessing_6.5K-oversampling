#include "../common/video_front_observation.h"
#include "../common/video_sensor_observation.h"
#include "../common/video_geometry_observation.h"
#include "../common/video_fullwidth_observation.h"
#include "../common/video_runtime_observation.h"
static uint64_t vf_poll_at,vf_record_at,vf_samples,vf_records,vf_changed,vf_unavailable;
static struct vf_sample vf_previous;
static struct vs_sample vs_previous;static uint64_t vs_record_at;
static struct vg_sample vg_previous;static uint64_t vg_record_at;
static struct vfo_sample vfo_previous;static uint64_t vfo_poll_at,vfo_record_at;
static struct vfr_sample vfr_previous;static uint64_t vfr_record_at;
static int vf_lens_read(void*ctx,uint32_t at,void*out,unsigned bytes){(void)ctx;return mapped_read(at,out,bytes,0);}
/* Four code-adjacent constants were refused by R9m's ordinary RAM reader.
 * Admit only the mapping and the two original per-frame capacity words and the guarded scaler slot. */
static int vfo_lens_read(void*ctx,uint32_t at,void*out,unsigned bytes){(void)ctx;int literal=bytes==4u&&(at==VFW_MAP_WORD||at==VFW_WORK_TABLE||at==VFW_SRC_TABLE||at==VFW_IPC_SETUP_WORD);return mapped_read(at,out,bytes,literal);}
static void video_front_sample(unsigned sequence){
 uint64_t begin=now();if(vf_poll_at&&begin-vf_poll_at<20000000u)return;vf_poll_at=begin;
 struct vf_sample s;(void)vf_capture(vf_lens_read,NULL,&s);vf_samples++;if(s.status==4)vf_changed++;if(s.status!=1)vf_unavailable++;
 if(!vf_record_at||memcmp(&s,&vf_previous,sizeof s)||begin-vf_record_at>=1000000000u){if(frame(context_file,16,sequence,begin,now(),&s,sizeof s)){vf_previous=s;vf_record_at=begin;vf_records++;}}
 struct vs_sample sensor;(void)vs_capture(vf_lens_read,NULL,&sensor);if(!vs_record_at||memcmp(&sensor,&vs_previous,sizeof sensor)||begin-vs_record_at>=1000000000u){if(frame(context_file,17,sequence,begin,now(),&sensor,sizeof sensor)){vs_previous=sensor;vs_record_at=begin;}}
 struct vg_sample geometry;(void)vg_capture(vf_lens_read,NULL,&geometry);if(!vg_record_at||memcmp(&geometry,&vg_previous,sizeof geometry)||begin-vg_record_at>=1000000000u){if(frame(context_file,18,sequence,begin,now(),&geometry,sizeof geometry)){vg_previous=geometry;vg_record_at=begin;}}
 if(!vfo_poll_at||begin-vfo_poll_at>=100000000u){vfo_poll_at=begin;struct vfo_sample profile;(void)vfo_capture(vfo_lens_read,NULL,&profile);if(!vfo_record_at||memcmp(&profile,&vfo_previous,sizeof profile)||begin-vfo_record_at>=1000000000u){if(frame(context_file,19,sequence,begin,now(),&profile,sizeof profile)){vfo_previous=profile;vfo_record_at=begin;}}
  struct vfr_sample runtime;(void)vfr_capture(vf_lens_read,NULL,&runtime);if(!vfr_record_at||memcmp(&runtime,&vfr_previous,sizeof runtime)||begin-vfr_record_at>=1000000000u){if(frame(context_file,20,sequence,begin,now(),&runtime,sizeof runtime)){vfr_previous=runtime;vfr_record_at=begin;}}
 }
}
static void video_front_schema(FILE*f){
 fprintf(f,"{\"format\":\"NX_VIDEO_FRONT_OBSERVATION1\",\"kind\":16,\"bytes\":%u,\"object_bytes\":932,\"root\":%u,\"secondary_root\":%u,\"expected_vtable\":%u,\"object_offsets\":{\"still_config\":632,\"video_config\":648,\"other_config\":664,\"fps_request_candidate\":684,\"sensor_interface_pointer\":604},\"raw_config_words_not_dimensions\":true,\"sensor_pixel_readout_proven\":false,\"scaler_geometry_proven\":false,\"sample_interval_us\":20000,\"atomic\":false,\"device_writes\":0,\"native_calls\":0}\n",(unsigned)sizeof(struct vf_sample),VF_ROOT,VF_SECONDARY_ROOT,VF_VTABLE);
}
static void video_sensor_schema(FILE*f){fprintf(f,"{\"format\":\"NX_VIDEO_SENSOR_OBSERVATION1\",\"kind\":17,\"bytes\":%u,\"object\":%u,\"expected_vtable\":%u,\"control_bytes\":112,\"object_offsets\":{\"requested_mode\":8,\"applied_mode\":9,\"geometry_candidate_0\":16,\"geometry_candidate_1\":20},\"source_getter\":\"0x8016c248\",\"set_mode\":\"0x8016bc34\",\"cached_state_only\":true,\"physical_sensor_readout_proven\":false,\"native_calls\":0,\"device_writes\":0}\n",(unsigned)sizeof(struct vs_sample),VS_OBJECT,VS_VTABLE);}
static void video_geometry_schema(FILE*f){fprintf(f,"{\"format\":\"NX_VIDEO_WINDOW_OBSERVATION1\",\"kind\":18,\"bytes\":%u,\"object\":%u,\"window_offset\":480,\"window_words\":22,\"native_consumer\":\"0x80164d04\",\"object_to_sensor_registers\":{\"0x208\":\"0x32ea\",\"0x20c\":[\"0x325a\",\"0x324a\"]},\"physical_sensor_readout_proven\":false,\"device_writes\":0}\n",(unsigned)sizeof(struct vg_sample),VS_OBJECT);}
static void video_front_summary(void){fprintf(events,"{\"event\":\"video_front_observation\",\"samples\":%llu,\"records\":%llu,\"changed_brackets\":%llu,\"unavailable\":%llu,\"native_calls\":0,\"device_writes\":0}\n",(unsigned long long)vf_samples,(unsigned long long)vf_records,(unsigned long long)vf_changed,(unsigned long long)vf_unavailable);}
static void video_runtime_schema(FILE*f){fprintf(f,"{\"format\":\"NX_VIDEO_RUNTIME_OBSERVATION1\",\"kind\":20,\"bytes\":128,\"sample_interval_us\":100000,\"u32\":[\"magic\",\"schema\",\"status\",\"mask\",\"driver_before\",\"driver_after\",\"scene_before\",\"scene_after\",\"counters[4]\",\"clock_before[9]\",\"clock_after[9]\",\"hash\",\"commit\"],\"counters\":[\"PP_EOF\",\"PP_WDMA0\",\"SSIF_SOF\",\"SSIF_EOF\"],\"clock_fields\":[\"PP\",\"PP_DMA\",\"IPC_A\",\"IPC_B\",\"IPC_DMA\",\"EP_A\",\"EP_B\",\"DP\",\"MBUS\"],\"clock_values_are_encoded_selectors\":true,\"native_scene_low_byte\":true,\"read_only\":true,\"atomic\":false,\"completion_requires_advancing_PP_counter\":true,\"profile_kind19_schema\":3,\"profile_kind19_bytes\":696}\n");}

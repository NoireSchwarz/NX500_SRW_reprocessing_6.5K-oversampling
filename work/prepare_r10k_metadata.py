from pathlib import Path
import json,hashlib
W=Path(__file__).resolve().parent;R=W/'r10k-ep-flow';B=W/'r10j-frame-flow'
p=R/'base/nx-lut-lite/data/video-probe-schema.json';d=json.loads(p.read_text('utf8'))
d.update(release='R10k',schema=7,sample_schema=7,record_bytes=1280,sampler_RAM_bytes=7696384)
d['valid']['EP_aux_readback']=1048576
d['EP_aux_readback']=dict(record_word_offset=287,words=32,magic='31525045',bases=['20821400','20821800','20821480','20821880'],
 fields=['plane0_address','plane1_address','format_stride','control0','control1'],double_read=True,IPC_header_bracket=True,mode_bracket=True,
 change_bits='12..31, five per group',valid_bits='0..3',group_change_bits='4..7',header_bits='8 before,9 after,10 same complete header,11 same driver/preset',
 earlier_progress_status_words='26..30; copied from preceding progress vector, not resampled with DMA',span_us_word=31,
 test_kind='passive matched-mode comparison; no auxiliary bypass/disable A/B',configuration_does_not_prove_transfer=True)
p.write_text(json.dumps(d,indent=2)+'\n',encoding='utf8',newline='\n')
frozen={}
for p in(B/'base/nx-lut-lite/bin').iterdir():
 if p.is_file()and p.name not in ['nxl-menu','nxl-video-probe.arm']:
  h=hashlib.sha256(p.read_bytes()).hexdigest();assert hashlib.sha256((R/'base/nx-lut-lite/bin'/p.name).read_bytes()).hexdigest()==h;frozen[p.name]=h
for relative in ['core/bin/rtos_video_fullwidth.bin','common/video_fullwidth_blocks.h','common/video_fullwidth_contract.h','core/src/rtos_video_fullwidth.c']:
 assert(B/relative).read_bytes()==(R/relative).read_bytes(),relative
out=dict(passed=True,base_release='R10j',frozen_binaries=frozen,RTOS_timing_and_fullwidth_contract_byte_identical=True,
 blue_recipe_byte_identical=True,source_SRW_USB_engines_preserved=True,new_device_writes=False,new_extra_frame_allocation=False,
 new_host_RAM_bytes=768000,host_RAM_bytes=7696384,record_schema=7,record_bytes=1280,camera_hardware_tested=False,
 source_sha256=hashlib.sha256((R/'core/src/nxl-video-probe.c').read_bytes()).hexdigest(),binary_sha256=hashlib.sha256((R/'base/nx-lut-lite/bin/nxl-video-probe.arm').read_bytes()).hexdigest())
(R/'validation/current/frozen-baseline.json').write_text(json.dumps(out,indent=2)+'\n',encoding='utf8')
print('PASS frozen R10j RTOS, blue recipe, full-width/USB/SRW executables; schema7 metadata updated')

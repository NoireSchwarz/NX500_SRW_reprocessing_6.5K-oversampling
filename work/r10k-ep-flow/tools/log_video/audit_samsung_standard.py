"""Reproducible offline Samsung-original/NX500 comparison; never writes an SD.

Inputs are user-downloaded official files, preserved source, copied MP4, and
original firmware. ARM replay models RAM/libc, NOT the ISP, kernel or DMA.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import sys
import zipfile
import numpy as np
from samsung_standard import (encode, decode, quantize, normalize, primary_matrix,
    encode_linear_rgb, read_cube, sample_cube, verify_official_files)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class Bits:
    def __init__(self, data):
        self.data=data; self.pos=0
    def read(self, count):
        if self.pos+count>len(self.data)*8:
            raise ValueError('truncated SPS')
        value=0
        for _ in range(count):
            value=(value<<1)|((self.data[self.pos//8]>>(7-self.pos%8))&1)
            self.pos+=1
        return value
    def ue(self):
        zeros=0
        while not self.read(1):
            zeros+=1
            if zeros>31:
                raise ValueError('unbounded SPS exp-Golomb value')
        return (1<<zeros)-1+self.read(zeros)


def sps_depth(nal):
    if len(nal)<3 or ((nal[0]>>1)&63)!=33:
        raise ValueError('not an HEVC SPS')
    bits=Bits(nal[2:].replace(b'\x00\x00\x03', b'\x00\x00'))
    bits.read(4)
    layers=bits.read(3); bits.read(1); bits.read(96)
    flags=[(bits.read(1),bits.read(1)) for _ in range(layers)]
    if layers:
        bits.read(2*(8-layers))
    for profile, level in flags:
        if profile: bits.read(88)
        if level: bits.read(8)
    bits.ue()
    chroma=bits.ue()
    if chroma==3: bits.read(1)
    width,height=bits.ue(),bits.ue()
    if bits.read(1):
        for _ in range(4): bits.ue()
    return {'width':width,'height':height,'chroma_format_idc':chroma,
            'luma_bits':8+bits.ue(),'chroma_bits':8+bits.ue()}


def video_header(path):
    import av
    with av.open(str(path)) as container:
        ctx=container.streams.video[0].codec_context; data=ctx.extradata
        if ctx.name!='hevc' or len(data)<23 or data[0]!=1:
            raise ValueError('expected HEVC hvcC')
        sps=[]; pos=23
        for _ in range(data[22]):
            if pos+3>len(data): raise ValueError('truncated hvcC array')
            kind=data[pos]&63; count=int.from_bytes(data[pos+1:pos+3],'big'); pos+=3
            for _ in range(count):
                if pos+2>len(data): raise ValueError('truncated hvcC length')
                size=int.from_bytes(data[pos:pos+2],'big'); pos+=2
                if pos+size>len(data): raise ValueError('truncated hvcC NAL')
                nal=data[pos:pos+size]; pos+=size
                if kind==33: sps.append(sps_depth(nal))
        if not sps: raise ValueError('no original SPS')
        depth={'luma_bits':8+(data[17]&7),'chroma_bits':8+(data[18]&7)}
        if any(any(s[k]!=depth[k] for k in depth) for s in sps):
            raise ValueError('SPS and hvcC bit depths disagree')
        return {'file':str(path),'sha256':sha(path),'codec':ctx.name,'profile':ctx.profile,
                'pixel_format':ctx.format.name,'hvcC':depth,'original_SPS':sps,
                'primaries_tag':int(ctx.color_primaries),'transfer_tag':int(ctx.color_trc),
                'matrix_tag':int(ctx.colorspace),'range_tag':int(ctx.color_range)}


def native_nodes(path):
    text=Path(path).read_text('utf-8-sig')
    out=[]
    for name in ('log_x','log_y'):
        found=re.search(r'\b'+name+r'\[19\]\s*=\s*\{([^}]+)\}',text)
        if found is None: raise ValueError('native nodes not found')
        nodes=[int(x.strip().rstrip('u')) for x in found.group(1).split(',')]
        if len(nodes)!=19: raise ValueError('unexpected native node count')
        out.append(nodes)
    return out


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--repo',required=True,type=Path)
    ap.add_argument('--official',required=True,type=Path)
    ap.add_argument('--history',required=True,type=Path)
    ap.add_argument('--video',required=True,type=Path)
    ap.add_argument('--out',required=True,type=Path)
    args=ap.parse_args()
    root=args.repo/'work/r10k-ep-flow'
    protected=[root/'core/src/rtos_gamma_reload.c',root/'core/src/rtos_gamma_reload_blob.h',
               root/'core/src/nxl-gamma-reload.c',root/'base/nx-lut-lite/gamma-reload.sh',
               root/'base/nx-lut-lite/src/menu_video.inc',root/'core/src/rtos_cc.inc',
               root/'tools/log_video/generate.py']
    before={str(p):sha(p) for p in protected}
    sources=verify_official_files(args.official)
    decoder=read_cube(args.official/'Samsung+Log+to+Linear+1DLUT_v1.0.cube')
    rec709=read_cube(args.official/'Samsung+Log+to+Rec709+3DLUT_v1.0.cube')
    if (decoder.size,decoder.dimensions,rec709.size,rec709.dimensions)!=(4096,1,65,3):
        raise ValueError('official geometry changed')
    if not np.array_equal(decoder.data[:,0],decoder.data[:,1]) or not np.array_equal(decoder.data[:,1],decoder.data[:,2]):
        raise ValueError('official 1D channels differ')
    code=np.linspace(0,1,decoder.size)
    delta=decode(code)-decoder.data[:,0]
    assert np.max(abs(delta))<1.3e-5
    from generate import log_encode,log_decode
    dense=np.r_[np.linspace(-.051,.011,4000),np.geomspace(.011,12,4000)]
    assert np.array_equal(encode(dense),log_encode(dense))
    assert np.array_equal(decode(code),log_decode(code))
    anchors=[]
    for scene in [0,.01,.18,.9,1,12]:
        signal=float(encode(scene))
        row={'scene':scene,'signal':signal}
        for bits in [8,10]:
            for sample_range in ['full','legal']:
                row[f'code{bits}_{sample_range}']=int(quantize(signal,bits=bits,sample_range=sample_range))
        rounded=normalize(row['code8_full'],bits=8,sample_range='full')
        row['official_decode_from_8bit']=float(sample_cube(decoder,np.repeat(rounded,3))[0])
        row['official_Rec709_from_8bit']=sample_cube(rec709,np.repeat(rounded,3)).tolist()
        anchors.append(row)
    profiles=[]
    fits=json.loads(args.history.read_text('utf-8-sig'))['fits']
    fits=[f for f in fits if f['maximum_X_gap_constraint']==4095]
    assert [f['white_scene_reflection'] for f in fits]==[1,4,12]
    from gamma_replay import GammaModel
    grid=np.arange(16384,dtype=float)
    current_x,current_y=native_nodes(root/'core/src/rtos_gamma_reload.c')
    assert fits[0]['X']==current_x and fits[0]['Y']==current_y
    for fit in fits:
        white=fit['white_scene_reflection']; x,y=fit['X'],fit['Y']
        assert len(x)==len(y)==19 and x[0]==0 and x[-1]==16383
        assert np.all(np.diff(x)>0) and np.max(np.diff(x))<=4095
        ideal=encode(grid*white/16383)*1023
        error=np.interp(grid,x,y)-ideal
        banks=[]
        for bank in [0,1]:
            model=GammaModel(args.repo/'work/firmware/P7.bin',args.repo/'work/tooling/python')
            data=model.calculate(bank,x,y);model.submit(bank)
            assert model.effective[1:20]==x and model.effective[20:39]==y
            banks.append({'bank':bank,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),
                          'nodes_preserved':True,'DDR_writer_exact_match':True})
        profiles.append({'assumed_scene_at_native_input_maximum':white,'X':x,'Y':y,
                         'ideal_linear_interpolation_max_error_code10':float(np.max(abs(error))),
                         'original_ARM_replay':banks,'exposure_scale_calibrated':False,
                         'camera_profile_compatible':False})
    slope=A1_MID= .258984868/np.log(10)*.18/(.18+.0003645)*np.log(2)
    precision=[]
    scene=np.geomspace(.01,12,100000)
    for bits in [8,10]:
        y=encode(scene); samples=quantize(y,bits=bits,sample_range='full')
        recovered=decode(normalize(samples,bits=bits,sample_range='full'))
        precision.append({'bits':bits,'middle_gray_codes_per_stop':float(slope*((1<<bits)-1)),
                          'maximum_rounding_error_stops_scene_001_to_12':float(np.max(abs(np.log2(recovered/scene)))),
                          'scope':'ideal normalized curve and nearest integer quantization; no camera/codec noise'})
    matrix=primary_matrix('BT709','BT2020')
    red=np.array([1.,0.,0.])
    wrong=encode(red)@matrix.T
    ordering_error=float(np.max(abs(wrong-encode_linear_rgb(red,source_primaries='BT709')))*1023)
    with zipfile.ZipFile(args.official/'SamsungLog_to_CineLUT_Rec709_v1.0.zip') as z:
        bad=z.testzip()
        if bad: raise ValueError('creative ZIP CRC failed: '+bad)
        creative={'CRC_valid':True,'entries':z.namelist(),'extracted':False}
    after={str(p):sha(p) for p in protected}
    assert before==after
    result={'schema':'nx500.samsung-log-official-audit.v1',
            'state':'OFFICIAL_SPEC_VERIFIED_CAMERA_COMPATIBILITY_PENDING',
            'source_files':sources,'official_whitepaper_visual_pages':[5,6,7],
            'published_constants_equal_existing_generator':True,
            'official_decoder':{'rows':4096,'maximum_scene_error_published_formula':float(np.max(abs(delta))),
                                'max_error_index':int(np.argmax(abs(delta))),
                                'endpoints':decoder.data[[0,-1],0].tolist(),
                                'scope':'rounded published coefficients vs independent downloaded v1.0 decoder'},
            'official_Rec709':{'grid':65,'rows':len(rec709.data),'interpolation':'trilinear',
                              'camera_compatibility_proven':False},
            'anchors':anchors,'precision':precision,
            'linear_BT709_to_BT2020_D65':matrix.tolist(),
            'wrong_matrix_after_log_pure_red_error_code10':ordering_error,
            'native_candidates':profiles,'original_ARM_cases':6,
            'recorded_video':video_header(args.video),
            'current_native_log_scene_maximum_assumption':1,
            'scene1_to_scene12_scale_change_stops':float(np.log2(12)),
            'candidate_input_scale_is_not_a_bit_depth_conversion':True,
            'current_video_gamut_path_verified':False,
            'gamut_blocking_evidence':{
                'old_CC_module':'core/src/rtos_cc.inc',
                'old_CC_native_bank':1,'video_gamma_bank':0,
                'reason':'Still-only parameter path is not a verified continuous-video CC hook; row gain/difference format is not a plain 3x3 RGB array'},
            'creative_archive':creative,'protected_source_before':before,'protected_source_after':after,
            'source_hashes_unchanged':True,'SD_written':False,'camera_code_changed':False,
            'not_proven':['scene-linear scale at native Gamma input','per-channel video Gamma pixel response',
                          'sensor/WB/CC to BT2020 during video','actual 10-bit encoder input/output',
                          'official LUT color accuracy across exposures/illuminants']}
    args.out.mkdir(parents=True,exist_ok=False)
    (args.out/'official-audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n','utf-8')
    with (args.out/'published_curve_8bit_10bit.csv').open('w',encoding='ascii',newline='') as f:
        writer=csv.writer(f);writer.writerow(['scene','normalized_Samsung_Log','full8','full10','legal8','legal10'])
        xx=np.linspace(0,12,16384);yy=encode(xx)
        cols=[quantize(yy,bits=b,sample_range=r) for b,r in [(8,'full'),(10,'full'),(8,'legal'),(10,'legal')]]
        for i,(x,y) in enumerate(zip(xx,yy)):
            writer.writerow([f'{x:.12g}',f'{y:.12g}',*[int(c[i]) for c in cols]])
    print(json.dumps({'state':result['state'],'official_files':len(sources),
                      'formula_max_scene_error':float(np.max(abs(delta))),'ARM_cases':6,
                      'actual_video_bits':result['recorded_video']['original_SPS'],
                      'matrix_order_error_code10':ordering_error,'sources_unchanged':before==after,
                      'out':str(args.out)},ensure_ascii=False))


if __name__=='__main__':
    main()

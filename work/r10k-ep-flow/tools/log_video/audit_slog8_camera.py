"""Read-only R11p clean Log native evidence audit. Queue acceptance is not pixel proof."""
from pathlib import Path
import argparse,hashlib,json,re,struct
import numpy as np


def fnv(data):
    h=2166136261
    for x in data:h=((h^x)*16777619)&0xffffffff
    return h


def layout(root):
    h=(root/'common/gamma_reload.h').read_text('utf-8-sig')
    if '#define NG_SCHEMA 3u' not in h:raise ValueError('requires gamma schema3 header')
    fields={};offset=0
    for decl in re.findall(r'uint32_t ([^;]+);',h.split('struct ng_state {')[1].split('};')[0]):
        for word in decl.split(','):
            m=re.fullmatch(r'\s*(\w+)(?:\[(\w+)\])?\s*',word)
            name,n=m.groups();n=108 if n=='NG_WORDS'else int(n or 1)
            fields[name]=(offset,n);offset+=n*4
    blob=(root/'core/src/rtos_gamma_reload_blob.h').read_text('ascii').split('vfw_code[',1)[1].split('{',1)[1].rsplit('}',1)[0]
    blob=bytes(int(x,16)for x in re.findall(r'0x([0-9a-f]{2})',blob))
    return fields,offset,fnv(blob[:0x4000])


def state(path,fields,size):
    data=path.read_bytes()
    if len(data)!=size:raise ValueError('state size mismatch')
    out={}
    for name,(at,n)in fields.items():
        v=list(struct.unpack_from('<'+'I'*n,data,at));out[name]=v[0]if n==1 else v
    return out


def unpack_cc(words):
    out=[]
    for group in range(2):
        row=[]
        for j in range(9):
            half=group*10+j;v=(words[half//2]>>((half&1)*16))&2047
            row.append(v-2048 if v&1024 else v)
        out.append(np.array(row,dtype=np.int64).reshape(3,3))
    return out


def expected_cc(words):
    mix=np.array([[657881,345278,45417],[72454,964208,11914],[17188,92289,939099]],dtype=np.int64)
    out=list(words)
    for group,a in enumerate(unpack_cc(words)):
        s=mix@a;v=np.where(s>=0,(s+524288)>>20,-((-s+524288)>>20)).ravel()
        for j,x in enumerate(v):
            if not -1024<=x<=1023:raise ValueError('native coefficient outside signed11')
            half=group*10+j;word=half//2;shift=(half&1)*16
            out[word]=(out[word]&~(2047<<shift))|((int(x)&2047)<<shift)
    return out


def gamma_curves(words):
    raw=struct.pack('<108I',*words);curves=[]
    for k in range(2):
        q=struct.unpack_from('<108H',raw,k*216)
        curves.append(([x&32767 for x in q[3:22]],[(y&2047)-2048 if y&1024 else y&2047 for y in q[25:44]]))
    return curves


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--card',required=True,type=Path);ap.add_argument('--out',required=True,type=Path)
    args=ap.parse_args();root=Path(__file__).resolve().parents[2]
    fields,size,code_hash=layout(root)
    source=(root/'core/src/rtos_gamma_reload.c').read_text('utf-8-sig')
    nodes=[]
    for name in ['log_x','log_y']:
        m=re.search(r'\b'+name+r'\[19\]\s*=\s*\{([^}]+)\}',source)
        nodes.append([int(x.strip().rstrip('u'))for x in m.group(1).split(',')])
    rows=[];errors=[]
    dirs=sorted((args.card/'NXDEMO51').glob('GAMMA_R11P_*'),key=lambda p:p.name)
    for directory in dirs:
        for tag in ['before','after']:
            p=directory/(tag+'-state.bin')
            if not p.exists():continue
            try:
                snap=json.loads((directory/(tag+'-snapshot.json')).read_text('utf-8-sig'))
                s=state(p,fields,size)
                identified=s['magic']==0x3147524e and s['schema']==3 and s['code_hash']==code_hash
                consistent=bool(snap.get('ok') and snap.get('state_consistent')) and not(s['sequence']&1 or s['busy'] or s['parent_active'])
                candidate=identified and consistent and s['mode']==2 and not s['fault'] and s['queued_epoch']==s['epoch'] and s['video_epoch']==s['epoch'] and s['video_color_epoch']==s['epoch'] and s['video_queues']>0
                flags={}
                if candidate:
                    flags['color_exact_left_composition']=expected_cc(s['video_color_factory'])==s['video_color_target']
                    flags['color_hash_matches']=fnv(struct.pack('<10I',*s['video_color_target']))==s['video_color_hash']
                    flags['gamma_hash_matches']=fnv(struct.pack('<108I',*s['video_target']))==s['video_hash']
                    flags['both_gamma_branches_match']=all(x==nodes[0] and y==nodes[1]for x,y in gamma_curves(s['video_target']))
                    flags['explicit_reference_scale']=s['profile']==0x33524753 and s['scene_white_q16']==165174 and s['native_black']==0 and s['native_white']==16383
                    a=s['video_driver_modes']&255;b=(s['video_driver_modes']>>8)&255
                    flags['explicit_midgray_node']=2965 in s['effective_x'] and s['effective_y'][s['effective_x'].index(2965)]==625
                    flags['supported_video_context']=22<=a<=25 or 22<=b<=25
                    flags['different_registered_objects']=s['video_object']!=s['video_color_object'] and s['video_object']!=0 and s['video_color_object']!=0
                queued=candidate and all(flags.values())
                rows.append({'run':directory.name,'tag':tag,'release_identified':identified,'snapshot_consistent':consistent,
                             'mode':s['mode'],'epoch':s['epoch'],'video_epoch':s['video_epoch'],'video_color_epoch':s['video_color_epoch'],
                             'video_queues':s['video_queues'],'fault':s['fault'],'rollback_count':s['rollback_count'],
                             'rollback_failed':s['rollback_failed'],'checks':flags,'new_SLOG8_video_parameters_queued':queued,
                             'physical_reflectance_calibrated':False,'hardware_pixel_consumption_proven':False})
            except (OSError,ValueError,KeyError,struct.error)as e:
                errors.append({'file':str(p),'error':str(e)})
    result={'format':'NXL_SLOG8_CAMERA_AUDIT1','release':'R11p','expected_profile':0x33524753,'expected_linear_gain':2.5203589432187403,'expected_code_hash':code_hash,'state_bytes':size,'records':rows,'read_errors':errors,
            'valid_video_queue_records':sum(r['new_SLOG8_video_parameters_queued']for r in rows),
            'hardware_pixel_consumption_proven':False,'official_LUT_pixel_accuracy_proven':False,
            'scope':'parameter/source/epoch association only; decoded calibration images must be checked separately',
            'SD_written':False}
    args.out.parent.mkdir(parents=True,exist_ok=True)
    with args.out.open('x',encoding='utf-8')as f:json.dump(result,f,ensure_ascii=False,indent=2);f.write('\n')
    print(json.dumps({'records':len(rows),'valid_video_queue_records':result['valid_video_queue_records'],'read_errors':len(errors),'hardware_pixels_verified':False}))


if __name__=='__main__':main()

"""Write a metadata-correct COPY of confirmed NX500 Samsung Log footage.

No colour conversion or re-encoding is performed. This cannot turn factory
or R11h1 footage into SLOG8. Original video and all coded pictures/audio stay
unchanged. Camera originals retain OEM labels; manual unmanaged/full-range
LUT use is also possible without this utility. Requires PyAV with hevc_metadata.
"""
from pathlib import Path
import argparse,hashlib,json,os
from fractions import Fraction
import av


def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb')as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()


def nals(data,length_size):
    at=0
    while at<len(data):
        if at+length_size>len(data):raise ValueError('truncated NAL length')
        n=int.from_bytes(data[at:at+length_size],'big');at+=length_size
        if n<2 or at+n>len(data):raise ValueError('invalid NAL extent')
        yield data[at:at+n];at+=n


def content_signature(path):
    rows=[]
    with av.open(str(path))as c:
        kinds=[s.type for s in c.streams]
    for index,kind in enumerate(kinds):
        if kind not in ('video','audio'):raise ValueError('unsupported extra stream; refuse to drop it')
        # Fresh demux per stream preserves negative-PTS audio priming packets.
        with av.open(str(path))as c:
            s=c.streams[index];h=hashlib.sha256();timeline=[];packets=0;vcl=0
            length=1+(s.codec_context.extradata[21]&3)if kind=='video'else 0
            for p in c.demux(s):
                if p.dts is None:continue
                if kind=='video':
                    for nal in nals(bytes(p),length):
                        if (nal[0]>>1)&63<32:h.update(len(nal).to_bytes(4,'big'));h.update(nal);vcl+=1
                else:h.update(len(bytes(p)).to_bytes(4,'big'));h.update(bytes(p))
                timeline.append([str(Fraction(p.pts)*p.time_base)if p.pts is not None else None,
                                 str(Fraction(p.dts)*p.time_base),str(Fraction(p.duration)*p.time_base)])
                packets+=1
            rows.append({'type':kind,'codec':s.codec_context.name,'packets':packets,'coded_picture_or_audio_sha256':h.hexdigest(),
                         'video_slice_NALs':vcl,'timeline':timeline})
    return rows


def container_colr(path):
    """Read colr only inside the video sample entry, never search mdat bytes."""
    rows=[]
    with Path(path).open('rb') as f:
        def walk(lo,hi):
            at=lo
            while at+8<=hi:
                f.seek(at);header=f.read(8);size=int.from_bytes(header[:4],'big');kind=header[4:];skip=8
                if size==1:size=int.from_bytes(f.read(8),'big');skip=16
                if size==0:size=hi-at
                if size<skip or at+size>hi:raise ValueError('invalid MP4 box extent')
                content=at+skip;end=at+size
                if kind in [b'moov',b'trak',b'mdia',b'minf',b'stbl']:walk(content,end)
                elif kind==b'stsd':walk(content+8,end)
                elif kind in [b'hvc1',b'hev1']:walk(content+78,end)
                elif kind==b'colr':
                    f.seek(content);b=f.read(min(end-content,11))
                    if b[:4] in [b'nclc',b'nclx'] and len(b)>=10:
                        rows.append(dict(type=b[:4].decode(),primaries=int.from_bytes(b[4:6],'big'),transfer=int.from_bytes(b[6:8],'big'),matrix=int.from_bytes(b[8:10],'big'),full_range=bool(b[10]&128)if b[:4]==b'nclx'and len(b)==11 else None))
                at=end
        walk(0,Path(path).stat().st_size)
    return rows


def copy_tags(source,output):
    source=Path(source).resolve();output=Path(output).resolve()
    if source==output or output.exists():raise ValueError('output must be a new file')
    if output.suffix.lower()!='.mp4':raise ValueError('output must be MP4')
    original=digest(source);before=content_signature(source)
    output.parent.mkdir(parents=True,exist_ok=True)
    staged=output.with_name(output.stem+'.partial.mp4')
    if staged.exists():raise ValueError('partial output already exists; preserve it')
    # Exclusive creation prevents overwriting any file even under a race.
    with av.open(str(source))as inp, staged.open('xb')as handle:
        streams=inp.streams
        if len(streams.video)!=1 or streams.video[0].codec_context.name!='hevc':raise ValueError('expected one HEVC stream')
        video=streams.video[0];e=video.codec_context.extradata
        if len(e)<23 or e[0]!=1 or (e[1]&31)!=1 or (e[17]&7) or (e[18]&7):raise ValueError('expected original Main/8bit HEVC')
        if video.codec_context.colorspace!=1 or video.codec_context.color_range!=2:raise ValueError('expected OEM BT709 matrix/full-range carrier')
        # Pinned PyAV/FFmpeg: explicitly write colr when Samsung Log transfer
        # is unspecified; validate the atom and SPS-derived values before publish.
        with av.open(handle,mode='w',format='mp4',options={'movflags':'+write_colr'})as out:
            mapping={s.index:out.add_stream_from_template(s,opaque=True)for s in streams}
            for s in streams:
                mapping[s.index].metadata.update(s.metadata)
            ov=mapping[video.index]
            if any(x.codec_context.is_encoder for x in mapping.values()):raise ValueError("refuse encoder contexts in metadata-only copy")
            bsf=av.bitstream.BitStreamFilterContext('hevc_metadata=colour_primaries=9:transfer_characteristics=2:matrix_coefficients=1:video_full_range_flag=1',in_stream=video,out_stream=ov)
            ov.codec_context.color_primaries=9;ov.codec_context.color_trc=2;ov.codec_context.colorspace=1;ov.codec_context.color_range=2
            out.metadata.update(inp.metadata);out.metadata['comment']='NX500 SGR3 Samsung Log 2025; transfer explicitly unspecified in H.273; BT.2020 D65; 8bit full; OEM BT.709 YCbCr matrix; tags-only copy'
            for packet in inp.demux():
                if packet.dts is None:continue
                index=packet.stream.index
                if index==video.index:
                    for p in bsf.filter(packet):p.time_base=video.time_base;p.stream=ov;out.mux(p)
                else:packet.stream=mapping[index];out.mux(packet)
            for p in bsf.filter(None):p.time_base=video.time_base;p.stream=ov;out.mux(p)
        handle.flush();os.fsync(handle.fileno())
    after=content_signature(staged)
    if before!=after:raise ValueError('coded pictures/audio or timing changed; keep partial file for inspection')
    if digest(source)!=original:raise ValueError('source changed during copy')
    with av.open(str(staged))as c:
        cc=c.streams.video[0].codec_context
        actual=[int(cc.color_primaries),int(cc.color_trc),int(cc.colorspace),int(cc.color_range)]
        if actual!=[9,2,1,2]:raise ValueError('output colour labels did not match request')
    colr=container_colr(staged)
    if colr!=[dict(type='nclx',primaries=9,transfer=2,matrix=1,full_range=True)]:raise ValueError('MP4 colr did not match requested labels')
    # Windows rename refuses an existing destination (no replace flag).
    if os.name!='nt':raise RuntimeError('this no-overwrite publisher is validated for Windows only')
    staged.rename(output)
    result={'passed':True,'source':str(source),'source_sha256':original,'output':str(output),'output_sha256':digest(output),
            'coded_pictures_audio_and_packet_times_identical':True,'output_tags':actual,'encoding_bits':8,'MP4_colr':colr,'PyAV_version':av.__version__,'FFmpeg_libraries':av.library_versions,
            'colour_conversion_performed':False,'reencoded':False,'camera_Log_profile_validated_by_this_tool':False}
    with output.with_suffix('.validation.json').open('x',encoding='utf-8')as f:json.dump(result,f,ensure_ascii=False,indent=2);f.write('\n')
    return result


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--confirmed-samsung-log','--confirmed-r11i-slog8',dest='confirmed_samsung_log',required=True,type=Path,help='only confirmed Log-direct footage; never an in-camera restored/graded clip')
    ap.add_argument('--out',required=True,type=Path);a=ap.parse_args()
    print(json.dumps(copy_tags(a.confirmed_samsung_log,a.out),ensure_ascii=False))


if __name__=='__main__':main()

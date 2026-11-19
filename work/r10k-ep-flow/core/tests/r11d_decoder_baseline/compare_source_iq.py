"""Offline decoding of bounded R9b source CC/Gamma evidence. No camera writes."""
from pathlib import Path
import argparse,struct,json,zlib
def words(b):return list(struct.unpack('<'+'I'*(len(b)//4),b))
def signed(v,bits):return v-(1<<bits)if v&(1<<(bits-1))else v
def cc_fields(w):
 h=struct.unpack('<20H',struct.pack('<10I',*w));return[signed(v&2047,11)for i,v in enumerate(h)if i%10!=9]
def fnv(b):
 v=2166136261
 for x in b:v=((v^x)*16777619)&0xffffffff
 return v
def analyze(front):
 c=(front/'selected-c0.bin').read_bytes();ca=words((front/'source-cc-audit.bin').read_bytes());ga=words((front/'gamma-test-audit.bin').read_bytes());assert len(c)==2048 and len(ca)==1024 and len(ga)==512
 assert ca[2]==ga[2] and ga[4]==ca[63] and zlib.crc32(struct.pack('<63I',*ga[:63]))&0xffffffff==ga[63]
 wb=words((front/'source-wb-audit.bin').read_bytes());assert len(wb)==256 and ga[7]==wb[47]==fnv(c)
 samples=(front/'isp-config-stages.bin').read_bytes();assert len(samples)==6*11920;stages=[]
 for i in range(6):
  b=samples[i*11920:(i+1)*11920];s=words(b);assert s[0]==0x31435049 and s[1]==1 and s[2]==ga[2] and s[3]==i+1 and fnv(b[:-8])==s[-2] and s[-1]==(s[0]^s[2]^s[3]^s[-2])
  banks=[s[34:1506],s[1506:2978]];bank=banks[1];g=struct.unpack('<216H',struct.pack('<108I',*bank[0x1228//4:0x13d8//4]));source=cc_fields(bank[0x1200//4:0x1228//4]);stages.append(dict(stage=i+1,qualified=s[4]==31 and s[5]==1,source_CC_equal=source==list(struct.unpack_from('<18h',c,524)),CC=source,X19=[v&32767 for v in g[108+3:108+22]],Y19=[signed(v&2047,11)for v in g[108+25:108+44]]))
 return dict(schema=1,release='R9b',Gamma_mode=ga[3],source_X19=list(struct.unpack_from('<19I',c,356)),source_Y19=list(struct.unpack_from('<19i',c,432)),source_CC=list(struct.unpack_from('<18h',c,524)),Gamma_status=dict(ready=ga[64],called=ga[65],status=ga[66],target_match=ga[67],restore_called=ga[68],restore_status=ga[69],restored=ga[70],error=ga[71],original_config_loads=ga[72],after_start=ga[73],after_complete=ga[74],bank0_unchanged_after_setter=ga[76],before_original_load=ga[77],config_return=ga[78],bank0_unchanged_at_restore=ga[79]),CC_status=dict(called=ca[68],status=ca[67],restored=ca[66]),stages=stages,extra_parameter_reload_calls=0,hardware_pixel_effect_proven=False,limits=['Stable DDR data is not proof of SRAM adoption or pixel effect.','Use same-SRW FACTORY/SOURCE/LIFT JPEG comparison to test actual consumption.'])
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('front',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.write_text(json.dumps(analyze(a.front),indent=2)+'\n',encoding='utf8')

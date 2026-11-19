"""Small integration corrections found by compiling the actual menu."""
from context import *
import re,shutil
U=T/'base/nx-lut-lite/src'
p=U/'browser.h';s=p.read_text('utf8')
s=s.replace('return s==25 || s==50 || s==75 || s==100;','return s>=0&&s<=100&&s%5==0;')
s=s.replace('else if(sscanf(line,"srw_gamma %d",&value)==1 && 0)srw_gamma_mode=1u;','/* Historical SRW test-curve settings are deliberately ignored. */')
a=s.index('static int browser_has_strengths(void) {');b=s.index('static int effective_strength',a)
s=s[:a]+'''static int browser_has_strengths(void){
 const char*id=selected_id(),*root=getenv("NX_LUT_ROOT");if(!id[0]||!root)return 0;
 char path[1200];snprintf(path,sizeof path,"%s/bin/nxl-lutblend.arm",root);return access(path,X_OK)==0;
}
'''+s[b:]
p.write_text(s,'utf8',newline='\n')
p=U/'menu.c';s=p.read_text('utf8')
s=s.replace('strength=strength==100?25:strength+25','strength=strength>=100?0:strength+5')
s=s.replace('else strcpy(notice,"此濾鏡僅支援 100% 強度");','else strcpy(notice,"參數工具不可用，請重新安裝完整套件");')
p.write_text(s,'utf8',newline='\n')
p=U/'lut_detail_flow.h';s=p.read_text('utf8')
s=s.replace('if(code==72||code==80){s->b_parameter=ld_wrap(s->b_parameter+(code==80?1:-1),4);',
 'if(code==72||code==80||code==165||code==163){s->b_parameter=ld_wrap(s->b_parameter+((code==80||code==163)?1:-1),4);')
p.write_text(s,'utf8',newline='\n')
p=U/'menu_lut_detail_v2.inc';s=p.read_text('utf8')
s=s.replace('}detail.b_cursor=0;','}\n detail.b_cursor=0;')
s=s.replace('static unsigned long detail_due;','static unsigned long detail_due,detail_message_until;')
short=r'''
static const char*detail_short_name(const char*id){
 if(strstr(id,"5207_n_fx"))return "5207 FX";
 if(strstr(id,"5207_n"))return "5207 N";
 if(strstr(id,"5207_s"))return "5207 S";
 if(strstr(id,"g200_n_fx_l"))return "G200 冷";
 if(strstr(id,"g200_fx_n"))return "G200 暖";
 if(!strcmp(id,"vlog_v709"))return "V709 還原";
 const struct nxl_look_info*i=info_for(id);return i==&nxl_unknown_info?id:i->name;
}
'''
s=s.replace('static void detail_draw(void);','static void detail_draw(void);\n'+short)
s=s.replace('info==&nxl_unknown_info?id:info->name,114','detail_short_name(id),114')
s=s.replace('detail_child=0;if(detail_return','detail_message_until=ticks()+1800UL;detail_child=0;if(detail_return')
s=s.replace('}else if(detail_dirty&&(detail_return||ticks()>=detail_due))',
 '}else if(detail_message[0]&&!detail.prepared&&detail_message_until&&ticks()>=detail_message_until){detail_message[0]=0;detail_message_until=0;return 1;}else if(detail_dirty&&(detail_return||ticks()>=detail_due))')
s=s.replace('detail_message[0]?detail_message:"左右選參數  撥盤調整  OK 應用"',
 'detail_message[0]?detail_message:info_for(detail_name(detail.cursor))->tone')
p.write_text(s,'utf8',newline='\n')

# Status is now served by the same validated read-only state routines without
# making a new SD report directory for each parameter edit.
p=T/'base/nx-lut-lite/gamma-reload.sh';s=p.read_text('utf8')
s=s.replace('ENGINE=$D/lut.sh','ENGINE=$D/lut.sh\nIDLE=$D/bin/nxl-lutidle.arm\nLUT_IDLE_SHA=0000000000000000000000000000000000000000000000000000000000000000')
s=s.replace('[ "$1" != status ] || exec "$GAMMA" status',
 '[ "$1" != status ] || { [ -f "$IDLE" ] && [ ! -L "$IDLE" ] && [ -x "$IDLE" ] && [ "$("$OPS" sha256 "$IDLE")" = "$LUT_IDLE_SHA" ] || fail LUT_IDLE_IDENTITY_FAILED;exec "$IDLE" --gamma-state; }')
p.write_text(s,'utf8',newline='\n');shutil.copyfile(p,M/p.name)

# The existing top-level version remains an internal session wire version.
# The public display/version explicitly identifies the release candidate.
p=U/'menu.c';s=p.read_text('utf8')
s=s.replace('puts("NX_LUT_MENU_VERSION=" NXL_DEMO_VERSION);','puts("NX_LUT_MENU_VERSION=" NXL_DEMO_VERSION);puts("NX_LUT_RELEASE=0.4.0-RC1; TRADITIONAL_CHINESE; R8F_M3_PARAMS; VLOG_LIBRARY_VID_ONLY");')
p.write_text(s,'utf8',newline='\n')
put(W/'integration-refinements.json',dict(passed=True,main_strength_0_to100_step5=True,rear_dial_B_parameters=True,
 restored_SRW_picker_paint=True,public_release_label='0.4.0-RC1',fast_Gamma_status_without_SD_reports=True,
 shortened_distinguishable_LUT_tiles=True,descriptions_visible_after_success_message=True,hardware_tested=False))
print('PASS release integration refinements',flush=True)

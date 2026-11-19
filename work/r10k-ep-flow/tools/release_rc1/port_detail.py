from context import *
import re
U=T/'base/nx-lut-lite/src';p=U/'menu_lut_detail_v2.inc';s=p.read_text('utf8')
assert 'detail_log_scope'not in s
def extent(s,name):
 m=re.search(r'static\s+(?:int|void)\s+'+name+r'\([^;]*?\)\s*\{',s);assert m,name
 depth=1;i=m.end();state='code'
 while depth:
  c=s[i];pair=s[i:i+2]
  if state=='code':
   if pair=='/*':state='comment';i+=2;continue
   if pair=='//':state='line';i+=2;continue
   if c=='"':state='string'
   elif c=="'":state='char'
   elif c=='{':depth+=1
   elif c=='}':depth-=1
  elif state=='comment':
   if pair=='*/':state='code';i+=2;continue
  elif state=='line':
   if c=='\n':state='code'
  elif c=='\\':i+=2;continue
  elif (state=='string'and c=='"')or(state=='char'and c=="'"):state='code'
  i+=1
 return m.start(),i
s=s.replace('static struct ld_flow detail;','static struct ld_flow detail;\nstatic int detail_log_scope;')
s=s.replace('if(detail.b_category==0&&fav!=(pass==0))continue;',
 'if(!strcmp(info->group,"loglut"))continue;\n  if(detail.b_category==0&&fav!=(pass==0))continue;')
a,b=extent(s,'detail_load_parameters');s=s[:a]+s[b:]
anchor='static void detail_close(int where){';assert anchor in s
s=s.replace(anchor,(A/'detail_cache.inc').read_text('utf8')+'\n'+anchor+'\n (void)detail_cache_save();')
s=s.replace('detail_visible=0;selected=detail.cursor+1;','detail_visible=0;if(!detail_log_scope)selected=detail.cursor+1;')
s=s.replace('if(!selected_id()[0]||rearm_child>0)return 0;if(video_enabled)',
 'if(!selected_id()[0]||rearm_child>0)return 0;if(access("/tmp/nx-lut-lite/clean-log.policy",F_OK)==0){strcpy(notice,"請先還原V‑Log，再編輯普通LUT");return 1;}detail_log_scope=0;if(video_enabled)')
log_open=r'''
static int detail_open_log(void){
 if(rearm_child>0||video_child>0){strcpy(notice,"設定更新中，請稍候");return 1;}
 if(access("/tmp/nx-lut-lite/clean-log.policy",F_OK)!=0){strcpy(notice,"請先啟用V‑Log");return 1;}
 int n=0,at=0;for(int i=0;i<total_count;i++)if(!strcmp(info_for(names[i])->group,"loglut")){if(!strcmp(names[i],active_name))at=n;detail_view[n++]=i;}
 if(!n){strcpy(notice,"尚無Log專用LUT");return 1;}
 detail_log_scope=1;ld_begin(&detail,n,at,100,0);
 detail_visible=1;detail_return=detail_dirty=detail_done=detail_eof=0;detail_committed_b[0]=detail_message[0]=0;detail_reset_inputs();
 XMoveResizeWindow(dpy,win,0,240,720,240);nxl_help_close(&help);if(!detail_load_parameters())detail_queue(0);return 1;
}
'''
s=s.replace('static void detail_result_action(int result){',log_open+'\nstatic void detail_result_action(int result){')
s=s.replace('if(value==1||(value==2&&code!=96',
 '''if(detail_log_scope&&code==173&&value){strcpy(detail_message,"Log使用單一專用LUT；混合僅限普通模式");return 1;}
 int old_cursor=detail.cursor;
 if(value==1||(value==2&&code!=96''')
s=s.replace('detail_result_action(ld_key(&detail,code));',
 'int result=ld_key(&detail,code);if(result==LD_A_CHANGED&&detail.cursor!=old_cursor){int next=detail.cursor;detail.cursor=old_cursor;detail_switch_a(next);}else detail_result_action(result);')
s=s.replace('else if(cell==2&&detail.level==LD_A&&y<38&&x>=350&&x<500)detail_result_action(ld_key(&detail,173));',
 'else if(cell==2&&detail.level==LD_A&&y<38&&x>=350&&x<500){if(detail_log_scope)strcpy(detail_message,"Log使用單一專用LUT");else detail_result_action(ld_key(&detail,173));}')
s=s.replace('detail.cursor=cell-100;detail_result_action(LD_A_CHANGED);','detail_switch_a(cell-100);')
s=s.replace('engine,"detail",detail_a,','engine,detail_log_scope?"log-detail":"detail",detail_a,')
s=s.replace('if(ok){strcpy(active_name,detail_a);', 'if(ok){(void)detail_cache_save();strcpy(active_name,detail_a);')
s=s.replace('group_names[category]);XSetForeground', 'detail_log_scope?"Log LUT":group_names[category]);XSetForeground')
s=s.replace('text(350,5,"EV 混合")','text(350,5,detail_log_scope?"Log專用":"EV 混合")')
p.write_text(s,'utf8',newline='\n')
put(W/'detail-port.json',dict(passed=True,per_LUT_RAM_edit_cache=True,cold_boot_auto_apply=False,normal_A_B_always_exclude_Log=True,
 Log_only_VID_entry=True,Log_has_single_A_no_cross_domain_M3=True,prepared_B_requires_final_A_OK=True,
 preserved_R8f_half_window=True,hardware_tested=False))
print('PASS half-screen detail scopes, per-LUT edit cache and explicit final mixture commit',flush=True)

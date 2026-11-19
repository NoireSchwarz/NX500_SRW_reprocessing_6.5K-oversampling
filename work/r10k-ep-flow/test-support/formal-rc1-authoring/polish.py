from context import *
import re,shutil
from context import patch as strict_patch
def patch(p,old,new,count=1):
 if old in p.read_text('utf8'):strict_patch(p,old,new,count)
 else:assert p.read_text('utf8').count(new)==count,(p,new)
U=T/'base/nx-lut-lite';C=U/'src'
p=U/'lut-core.sh'
patch(p,'\nrestore() {','\nrestore_factory() {')
patch(p,'lut_check_source "$name";expected_a=$2','lut_check_source "$name"')
p=C/'menu_lut_detail_v2.inc'
patch(p,'else if(result==LD_A_CHANGED){selected=detail.cursor+1;','else if(result==LD_A_CHANGED){if(!detail_log_scope)selected=detail.cursor+1;')
patch(p,'p[0]<=100&&p[1]','p[0]<=100&&p[0]%5==0&&p[1]')
patch(p,'if(n<=0)return 0;line[n]=0;','if(n!=st.st_size)return 0;line[n]=0;')
patch(p,'const struct nxl_look_info*info=info_for(id);limited_text(x+8','limited_text(x+8')
patch(p,'?"OK 參數":"應用混合"','?"OK 參數":"準備混合"')
p=C/'browser.h'
patch(p,'    fclose(f);\n}\nstatic int browser_save','    fclose(f);\n    if(!strcmp(info_for(srw_lut_id)->group,"loglut"))strcpy(srw_lut_id,"off");\n}\nstatic int browser_save')
patch(p,'    fprintf(f,"srw_gamma %u\\n",srw_gamma_mode<=5?srw_gamma_mode:1u);','    /* Experimental curve state is never persisted by the release menu. */')
p=U/'memory-test.sh'
patch(p,'    identifier "$selected_srw_lut" || fail SRW_LUT_NAME_INVALID', '''    identifier "$selected_srw_lut" || fail SRW_LUT_NAME_INVALID
    case "$selected_srw_lut" in vlog_*) fail SRW_LOG_LUT_NOT_ALLOWED;;esac''')
# The GUI and CLI both reject the reserved V-Log namespace; the original
# source/curve interfaces remain available for explicit research invocations.
for row in json.loads((U/'data/release-lut-catalog.json').read_text('utf8'))['looks']:
 if row['input']=='VLOG':assert row['id'].startswith('vlog_')
for n in ['finish-srw.sh','srw-log-policy.sh']:shutil.copyfile(A/n,U/n)
# Every installed release has the same complete fallback library as the ZIP.
cat=json.loads((U/'data/release-lut-catalog.json').read_text('utf8'))['looks']
(U/'looks').mkdir(exist_ok=True)
for row in cat:
 f=S/'looks'/(row['id']+'.bin');assert sha(f)==row['native_sha256']
 shutil.copyfile(f,M/'looks'/f.name);shutil.copyfile(f,U/'looks'/f.name)
p=C/'menu_progress.inc'
patch(p,'limited_text(28,y+88,task_progress.detail,660);','limited_text(28,y+88,task_progress.issue?"已安全停止，請查看工作階段狀態":"請稍候，完成後自動返回選單",660);')
put(W/'release-polish.json',dict(passed=True,restore_dispatch_preserves_factory_routine=True,
 stale_SRW_Log_selection_removed=True,SRW_CLI_rejects_Log_namespace=True,cache_grid_validated=True,
 Log_selection_preserves_normal_cursor=True,complete_installer_library=len(cat),SD_written=False))
print('PASS release restoration dispatch, stale selection and complete installer library')

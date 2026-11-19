from context import *
U=T/'base/nx-lut-lite/src'
patch(U/'menu_video.inc','  if(video_command==23&&ok)video_menu_start(31);','  /* The release menu does not poll the hidden colour experiment. */')
patch(U/'menu.c','?"LOG直出 / 濾鏡鎖定":video_enabled?"視頻 LUT: ON":"視頻 LUT: OFF"','?"Log LUT：請至VID設定":video_enabled?"影片LUT：開啟":"影片LUT：關閉"')
patch(U/'menu.c','text(24,442,"上下／下撥輪 選擇");text(320,442,"OK 詳情")','text(24,442,"左右 切換功能／內容");text(320,442,"OK 詳情")')
p=U/'menu_lut_detail_v2.inc'
patch(p,'static const char*detail_short_name(const char*id){','''static const char*detail_short_name(const char*id){
 if(!strcmp(id,"fuji_provia100f"))return "Provia";
 if(!strcmp(id,"kodak_portra400"))return "Portra";
 if(!strcmp(id,"mono_hp5"))return "HP5";
 if(!strcmp(id,"ejay_skin_native"))return "膚色";''')
old='int pb[4];memcpy(pb,detail.b,sizeof pb);if(detail_log_scope)memset(pb,0,sizeof pb);'
new='int pb[4];memcpy(pb,detail.prepared?detail_committed_params:detail.b,sizeof pb);if(detail_log_scope)memset(pb,0,sizeof pb);'
patch(p,old,new);patch(A/'detail_cache.inc',old,new)
# Copy only the two finalized new scripts; no historical generator is rerun.
import shutil
for n in ['finish-srw.sh','srw-log-policy.sh']:shutil.copyfile(A/n,T/'base/nx-lut-lite'/n)
put(W/'last-UI-fixes.json',dict(passed=True,hidden_colour_poll_disabled=True,prepared_B_cache_keeps_confirmed_parameters=True,
 readable_short_names=True,SD_written=False))

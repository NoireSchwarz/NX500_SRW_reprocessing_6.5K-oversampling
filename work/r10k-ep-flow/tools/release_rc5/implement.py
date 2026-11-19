from context import *
P=T/'base/nx-lut-lite';U=P/'src'
patch(P/'lut-core.sh',' request_a=$name;request_b=$other;keep_video=0\n [ ! -f "$ST/video_enabled" ] || keep_video=1',' request_a=$name;request_b=$other')
patch(P/'lut-core.sh','  if [ "$LUT_MODE" = VLOG ] || [ "$keep_video" = 1 ];then video_enable;fi','  # A confirmed ordinary or Log LUT also applies to new video recordings.\n  video_enable')
# Decide the shortcut from verified runtime state while holding the existing
# Gamma command lock. A pending or active Log route always offers restore.
p=P/'gamma-reload.sh'
patch(p,'status|lift|log|restore)','status|lift|log|restore|toggle)')
patch(p,'case "$1" in\nlog)','''if [ "$1" = toggle ];then
 if [ -e "$RUN/clean-log.policy" ] || [ -L "$RUN/clean-log.policy" ];then set -- restore
 else
  [ -f "$IDLE" ] && [ ! -L "$IDLE" ] && [ -x "$IDLE" ] && [ "$("$OPS" sha256 "$IDLE")" = "$LUT_IDLE_SHA" ] || fail LUT_IDLE_IDENTITY_FAILED
  "$IDLE" --gamma-state > "$RUN/gamma-toggle-state.txt" 2>&1 || fail GAMMA_TOGGLE_STATE_UNAVAILABLE
  grep -qx 'NXL_GAMMA_RELOAD1' "$RUN/gamma-toggle-state.txt" && grep -qx 'end=NXL_GAMMA_RELOAD_END1' "$RUN/gamma-toggle-state.txt" || fail GAMMA_TOGGLE_STATE_INVALID
  if grep -qx 'mode=FACTORY' "$RUN/gamma-toggle-state.txt" && grep -Eq '^phase=(OFF|FACTORY_QUEUED)$' "$RUN/gamma-toggle-state.txt" && grep -qx 'fault=0' "$RUN/gamma-toggle-state.txt";then set -- log
  else set -- restore;fi
 fi
fi
case "$1" in
log)''')
patch(U/'ev_triple.h','if(++s->count==3)','if(++s->count==2)')
for n in ['keys.c','keys.h']:
 p=U/n;s=p.read_text('utf8').replace('NXL_TOGGLE_VIDEO','NXL_TOGGLE_VLOG').replace('--video-toggle','--vlog-toggle').replace('VIDEO_LAUNCH','VLOG_LAUNCH').replace('EV_TRIPLE_CURRENT_LUT','EV_DOUBLE_CURRENT_LUT').replace('EV+Wi-Fi video listener','EV+Wi-Fi VLog listener');p.write_text(s,'utf8',newline='\n')
p=U/'keys.c'
patch(p,'if(!command)continue;','if(!command)continue;\n                if(command==NXL_TOGGLE_VLOG&&menu_active())continue;')
patch(p,'memset(&shortcut, 0, sizeof shortcut);','memset(&shortcut,0,sizeof shortcut);memset(&taps,0,sizeof taps);')
p=U/'menu.c'
patch(p,'static int detail_open_log(void);','static int detail_open_log(void);\nstatic int detail_open_current(void);\nstatic int detail_visible;')
patch(p,'static struct nxl_ev_taps menu_ev_taps;','static struct nxl_ev_taps menu_ev_taps;\nstatic struct nxl_shortcut menu_shortcut;')
patch(p,' unsigned long now=ticks();',''' unsigned long now=ticks();
 int prior_chord=menu_shortcut.pending,command=nxl_activate(&menu_shortcut,code,value);
 if(prior_chord||menu_shortcut.pending||command){
  if(value!=2)down[code]=value!=0;
  memset(&menu_ev_taps,0,sizeof menu_ev_taps);combo_block=menu_shortcut.chord.ev||menu_shortcut.chord.ael||menu_shortcut.wifi;
  if(command==NXL_TOGGLE_VLOG){
   if(detail_child||detail_dirty||rearm_child||video_child)strcpy(notice,"設定更新中，請稍候");
   else video_menu_start(24);
  }
  return;
 }''')
patch(p,'memset(&menu_ev_taps,0,sizeof menu_ev_taps);memset(down,0,sizeof down);combo_block=0;','memset(&menu_ev_taps,0,sizeof menu_ev_taps);memset(&menu_shortcut,0,sizeof menu_shortcut);memset(down,0,sizeof down);combo_block=0;')
patch(p,'    int video_shortcut=argc>1','    int vlog_shortcut=argc>1&&!strcmp(argv[1],"--vlog-toggle");\n    int video_shortcut=argc>1')
patch(p,'        if(video_shortcut) {toast','        if(video_shortcut||vlog_shortcut) {toast')
patch(p,'    if(video_shortcut) {\n','''    if(vlog_shortcut){
        video_menu_start(24);
        if(video_child<=0){toast(notice,0);close(lock);return 1;}
        while(video_child>0){if(quitting)kill(video_child,SIGTERM);video_menu_poll();if(video_child>0)poll(NULL,0,50);}
        toast(gamma_command_failed?notice:gamma_status.mode==2?"V‑Log已開啟":"V‑Log已關閉",!gamma_command_failed);close(lock);return gamma_command_failed?1:0;
    }
    if(video_shortcut) {
''')
p=U/'menu_video.inc'
patch(p,'command==23?"gamma-status":','command==23?"gamma-status":command==24?"gamma-toggle":')
patch(p,'if(video_command==21||video_command==22)refresh_lut_runtime();','if(video_command==21||video_command==22||video_command==24)refresh_lut_runtime();\n  if(ok&&detail_visible&&(video_command==21||video_command==22||video_command==24))detail_open_current();')
# Keep the release number coherent, without touching native code or LUT data.
for p in U.iterdir():
 if p.suffix in ['.c','.h','.inc']and p.name!='ui_font.h':
  s=p.read_text('utf8');new=s.replace('0.4.0 RC4','0.4.0 RC5').replace('0.4.0-RC4','0.4.0-RC5')
  if s!=new:p.write_text(new,'utf8',newline='\n')
print('PASS RC5 default video and EV shortcut controls')

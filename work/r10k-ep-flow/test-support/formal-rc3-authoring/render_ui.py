from pathlib import Path
here=Path(__file__).resolve().parent
s=(here.parent/'formal-rc1-authoring/render_ui.py').read_text('utf8')
s=s.replace("picker=(U/'menu_lut_picker.inc').read_text('utf8')", "picker=(U/'menu_lut_picker.inc').read_text('utf8');vp=(U/'menu_video_picker.inc').read_text('utf8')")
s=s.replace('static int detail_visible,detail_hidden,', 'static int video_picker_open,video_picker_cursor;static const char*video_picker_titles[]={"全寬UHD 24p","UHD轉2.5K","恢復原廠UHD"};\nstatic int detail_visible,detail_hidden,')
s=s.replace('static void publish_ui(void){}','static void publish_ui(void){}\nstatic void menu_modal_input_release(void){}')
s=s.replace("for name in ['release_video_count','release_video_index','draw_video']", "src+=function(vp,'draw_video_picker')\nfor name in ['release_video_count','release_video_index','draw_video']")
s=s.replace('if(mode==3){left_panel=2;video_row=8;log_active=1;draw_video();}', 'if(mode==3){left_panel=2;video_row=9;video_picker_open=1;video_picker_cursor=0;draw_video();}')
s=s.replace('03-VID-page1','03-VID-main').replace('04-VID-page2','04-VID-mode-picker')
s=s.replace('  detail_draw();}', '  if(mode==8||mode==9){for(int i=0;i<count;i++)if(!strcmp(names[detail_view[i]],mode==8?"xh_5219_bright":"xh_5219_hasselblad"))detail.cursor=i;}detail_draw();}')
s=s.replace("'08-Log-half']", "'08-Log-half','09-XH-Bright','10-XH-Hasselblad']")
s=s.replace('enumerate([0,1,2,3,4,7])','enumerate([1,2,3,8,9,7])')
exec(compile(s,str(here/'render_ui.py'),'exec'))

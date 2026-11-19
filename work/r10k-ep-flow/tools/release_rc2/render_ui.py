"""Reuse the recorded-X11 renderer with RC2 actual drawing functions."""
from pathlib import Path
here=Path(__file__).resolve().parent
support=here/'rc1-test-support/render_ui.py'
if not support.exists():support=here.parent/'formal-rc1-authoring/render_ui.py'
s=support.read_text('utf8')
s=s.replace("picker=(U/'menu_lut_picker.inc').read_text('utf8')", "picker=(U/'menu_lut_picker.inc').read_text('utf8');vp=(U/'menu_video_picker.inc').read_text('utf8')")
s=s.replace('static int detail_visible,detail_hidden,', 'static int video_picker_open,video_picker_cursor;static const char*video_picker_titles[]={"全寬UHD 24p","UHD轉2.5K","恢復原廠UHD"};\nstatic int detail_visible,detail_hidden,')
s=s.replace("for name in ['release_video_count','release_video_index','draw_video']", "src+=function(vp,'draw_video_picker')\nfor name in ['release_video_count','release_video_index','draw_video']")
s=s.replace('if(mode==3){left_panel=2;video_row=8;log_active=1;draw_video();}', 'if(mode==3){left_panel=2;video_row=9;video_picker_open=1;video_picker_cursor=0;draw_video();}')
s=s.replace('03-VID-page1','03-VID-main').replace('04-VID-page2','04-VID-mode-picker')
exec(compile(s,str(Path(__file__).resolve()),'exec'))

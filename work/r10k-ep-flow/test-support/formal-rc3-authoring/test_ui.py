from pathlib import Path
here=Path(__file__).resolve().parent
s=(here.parent/'formal-rc2-authoring/test_ui.py').read_text('utf8')
s=s.replace("A.parent/'formal-rc1-authoring/cfunc.py'","A.parent/'formal-rc2-authoring/cfunc.py'")
s=s.replace('static void task_child_watch_detach(void){}','static void task_child_watch_detach(void){}\nstatic void menu_input_child_close(void){}\nstatic int modal_available=1;static int menu_modal_input_take(void){return modal_available;}')
s=s.replace('int main(void){assert(detail_open()', 'int main(void){modal_available=0;assert(detail_open()&&!detail_visible&&!detail_child&&!detail_dirty);modal_available=1;assert(detail_open()',1)
# The second navigation fixture does not include the asynchronous detail worker.
s=s.replace('static void video_menu_start(int n)', 'static int menu_modal_input_take(void){return 1;}\nstatic void video_menu_start(int n)')
exec(compile(s,str(here/'test_ui.py'),'exec'))

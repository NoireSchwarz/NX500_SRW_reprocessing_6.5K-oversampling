from context import *
import shutil
shutil.copyfile(A.parent/'formal-rc2-authoring/cfunc.py',A/'cfunc.py')
s=(A.parent/'formal-rc3-authoring/render_ui.py').read_text('utf8')
insert='''
s=s.replace("'10-XH-Hasselblad']", "'10-XH-Hasselblad','11-Factory-current','12-PureLog-current']")
s=s.replace('detail_draw();}', 'if(mode==10||mode==11){int n=1;detail_view[0]=-1;for(int i=0;i<total_count;i++)if((!strcmp(info_for(names[i])->group,"loglut"))==(mode==11))detail_view[n++]=i;detail_log_scope=mode==11;ld_begin(&detail,n,0,0,0);strcpy(detail_message,"目前設定；修改後按OK裝載");}detail_draw();}')
s=s.replace("im=images[c];ImageDraw.Draw(im)","if not w or not h:continue\\n  im=images[c];ImageDraw.Draw(im)")
'''
s=s.replace("exec(compile(s,str(here/'render_ui.py'),'exec'))",insert+"\nexec(compile(s,str(here/'render_ui.py'),'exec'))")
exec(compile(s,str(A/'render_ui.py'),'exec'))

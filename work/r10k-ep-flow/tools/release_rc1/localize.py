"""Traditional-Chinese display strings and an extended original font subset."""
from context import *
import ctypes,math,re
sys.path.insert(0,str(PY))
from PIL import Image,ImageDraw,ImageFont
from fontTools.ttLib import TTFont
U=T/'base/nx-lut-lite/src';D=T/'base/nx-lut-lite/data'
def trad(s):
 n=ctypes.windll.kernel32.LCMapStringEx('zh-TW',0x04000000,s,len(s),None,0,None,None,0)
 if not n:return s
 out=ctypes.create_unicode_buffer(n);assert ctypes.windll.kernel32.LCMapStringEx('zh-TW',0x04000000,s,len(s),out,n,None,None,0)==n
 return out.value
translations={
 'AUTO INITIALIZE':'初始化並重顯影','STAGE STATUS':'處理進度','SRW / LUT / JPEG':'SRW重顯影','STAGE CHANGES ONLY - AUTO RETURN TO EV+AEL':'完成後自動返回EV+AEL選單',
 'Starting initialization recorder':'準備初始化','Reading initial Still state':'讀取原廠顯影狀態','Testing original EP lifecycle':'核驗原廠處理流程','Calling original EP open':'準備原廠影像處理','Returning the EP reference':'歸還原廠處理資源','Verifying code and reference return':'核驗程式與資源歸還','Checking final Still readiness':'核驗顯影準備狀態','Saving initialization evidence':'儲存初始化結果','Initialization complete':'初始化完成','Test saved - Still is not ready':'結果已儲存，顯影尚未就緒',
 'Starting the workflow':'準備重顯影','Checking task and camera':'核驗工作階段與相機','Preparing shared workspace':'準備工作空間','Reading SRW':'讀取SRW原檔','Verifying source and camera settings':'核對原檔與拍攝設定','Decoding SRW pixels':'解碼SRW影像','Running native Still ISP':'執行原廠顯影','Verifying Still handoff':'核驗顯影輸出','Still output ready':'顯影輸出已就緒','Applying native lens correction':'套用鏡頭校正','Applying native LUT':'套用LUT','Encoding native JPEG':'輸出JPEG','JPEG stream written':'JPEG已寫入','Checking callback and DMA retirement':'核驗影像處理已結束','Callbacks and DMA are quiet':'影像處理已結束','Returning the shared workspace':'歸還工作空間','Verifying original allocator return':'核驗原廠記憶體歸還','Workspace return verified':'工作空間已歸還','Final JPEG saved':'最終JPEG已儲存','Verifying the saved JPEG':'核驗已儲存的JPEG','Restoring the camera':'恢復原廠取景','Checking native idle state':'核驗相機待機狀態',
 'Starting resource check':'開始核驗資源','Checking current task':'核驗目前工作階段','Reading resource records':'讀取資源紀錄','Verifying original code':'核驗原廠程式','Checking allocator / Still 1 of 3':'核驗顯影資源1／3','Checking allocator / Still 2 of 3':'核驗顯影資源2／3','Checking allocator / Still 3 of 3':'核驗顯影資源3／3','Checking DMA / queues 1 of 16':'核驗輸出資源1／16','Checking DMA / queues 5 of 16':'核驗輸出資源5／16','Checking DMA / queues 9 of 16':'核驗輸出資源9／16','Checking DMA / queues 13 of 16':'核驗輸出資源13／16','Rechecking the allocator':'再次核驗記憶體歸還','Renewing round locks':'更新工作階段鎖定','Renewing front permission':'更新顯影準備狀態','Creating the fresh task':'建立新工作階段','Checking resources for initialization':'核驗初始化資源','Fresh task ready for initialization':'新工作階段已準備','Camera ready for the shared pipeline':'相機已準備重顯影','Verifying source SRW and preparing pipeline':'核驗SRW並準備處理','Starting initialization pipeline':'開始初始化與重顯影','Acquiring original EP reference':'取得原廠處理資源','Returning the test EP reference':'歸還原廠處理資源',
 'Starting SRW / LUT / JPEG':'開始SRW重顯影','Waiting for the first verified stage':'等待處理開始','Stopped - LDC DMA drain unverified':'已停止，鏡頭資源歸還未確認','Stopped - lens correction did not finish':'已停止，鏡頭校正尚未完成','Stopped - lens configuration readback failed':'已停止，鏡頭設定未確認','Stopped - lens correction failed':'已停止，鏡頭校正未完成','Stopped - resources kept safe':'已安全停止，請查看狀態','Checking failed task ownership':'核驗目前處理資源','Resource check passed':'資源核驗通過','JPEG saved - workspace returned':'JPEG已儲存，資源已歸還','Stopped - returning to EV+AEL menu':'已停止，返回EV+AEL選單',
 'SELECT LUT / FAVORITES':'選擇LUT／收藏','BACK':'返回','OK SELECT / MENU BACK':'OK 選擇／MENU 返回','No favorites yet':'尚未加入收藏',
 'STATUS UNAVAILABLE':'無法讀取狀態','BOOT INIT REQUIRED':'等待初始化','PIPELINE RUNNING':'重顯影執行中','OLD SPLIT JOB':'舊工作階段','BACKEND RUNNING':'JPEG輸出中','COMPLETE':'完成','RETRYABLE':'可重試','RESOURCE CHECK REQUIRED':'需要核驗資源',
 'RUN SRW/LUT/JPEG':'初始化並重顯影','NEW JOB REQUIRED':'需要新工作階段','RETRY PIPELINE':'重試重顯影','VIEW STATUS':'查看狀態','CHECK REPORT':'查看狀態','CHECK RESOURCES':'核驗資源','VERIFY RESOURCES':'核驗資源','REINSTALL PACKAGE':'請重新安裝',
 'BOOT: TAKE PHOTO + ALBUM EDIT / RESIZE':'開機後拍攝照片，再執行初始化','SRW / LUT / JPEG - ONE RUN, PLEASE WAIT':'SRW與JPEG處理中，請稍候','NATIVE.JPG SAVED - NEW SESSION TO REPEAT':'JPEG已儲存，可準備新工作階段','SAFE STOP - PREPARE CAMERA THEN RETRY':'已安全停止，請準備相機後重試','HARDWARE STATE HELD - CHECK PROGRESS / LOG':'資源尚未歸還，請查看狀態','SESSION STATUS FAILED - CHECK SD':'無法讀取狀態，請檢查記憶卡',
 'VIDEO UPDATE RUNNING - PLEASE WAIT':'影片設定更新中，請稍候','RESET CHECK RUNNING - PLEASE WAIT':'資源核驗中，請稍候','SESSION UPDATE RUNNING - PLEASE WAIT':'工作階段更新中，請稍候','SESSION RUNNING - PLEASE WAIT':'重顯影執行中，請稍候',
 'GAMMA RESPONSE INVALID - CHECK GAMMA LOG':'無法確認Log回應，請查看狀態','GAMMA NATIVE FAILURE - SELECT TO RESTORE':'Log更新未完成，請選擇還原','GAMMA WAIT: ENTER VIDEO; OK REQUESTS FACTORY':'等待原廠更新，可按OK還原','FACTORY GAMMA QUEUED - RECORD TO VERIFY':'已要求還原原廠曲線','LOG DIRECT / LOOKS LOCKED':'V‑Log已啟用，請從VID選Log LUT','FACTORY / SELECT: VLOG8':'原廠曲線／可啟用V‑Log',
 'READING VIDEO OUTPUT':'讀取影片設定','READING COLOR PARAMETERS':'讀取色彩狀態','UPDATING NATIVE ISP GAMMA':'更新Log曲線','UPDATING STAGE COUNTERS':'更新狀態','CHANGING VIDEO OUTPUT':'更新影片設定',
 'STOP RECORDING; GRADE ON COMPUTER':'切換前停止錄影並回到照片待機','VIDEO LUT ON - DISABLE IT FIRST':'請先關閉影片LUT','APPLY A LUT FIRST':'請先選擇並套用LUT','VIDEO LUT ON - START NEW RECORDING':'影片LUT已啟用，請開始新錄影','VIDEO LUT OFF - PHOTO LUT KEPT':'影片LUT已關閉，照片LUT保留','LUT OFF - SAVED STATE RESTORED':'LUT已還原','READY - HALF-PRESS THEN SHOOT':'已套用，半按快門載入後拍攝','FAILED - CHECK LAST-ACTION.LOG':'套用未完成，請查看狀態','LOG MODE - RESTORE GAMMA TO USE LOOKS':'請先還原V‑Log，再使用普通LUT',
 'RESTORING OEM CAMERA - PLEASE WAIT':'正在恢復原廠取景，請稍候','REAL OEM LIVEVIEW RESTORED':'原廠取景已恢復','RECOVERY UNCONFIRMED - CHECK LOGS':'尚未確認恢復，請查看狀態','CHECKING RAW RESOURCES - PLEASE WAIT':'正在核驗重顯影資源，請稍候','SESSION READY - BOOT INIT CAN BE REUSED':'新工作階段已準備，可沿用本次初始化','RESET NOT PROVEN - CHECK REARM LOG':'資源核驗未完成，請查看狀態',
 'DEMO51 SRW / LUT / JPEG - PLEASE WAIT':'SRW重顯影執行中，請稍候','NATIVE LUT / JPEG 422 - PLEASE WAIT':'LUT與JPEG輸出中，請稍候','YUV READY - CONTINUING LUT / JPEG':'顯影完成，繼續LUT與JPEG輸出','NATIVE.JPG SAVED - WORKSPACE RETURN VERIFIED':'JPEG已儲存，工作空間已歸還','HARDWARE HOLD - CHECK NXDEMO51/LOGS':'資源尚未歸還，請查看狀態','DEMO51 STOPPED - OPEN SESSION STATUS':'重顯影已停止，請查看狀態','DEMO51 SESSION BUSY':'工作階段忙碌中',
 'AUTO INITIALIZATION COMPLETE':'初始化與重顯影完成','INITIALIZATION STOPPED - CHECK LOGS':'初始化已停止，請查看狀態','NEW SESSION READY - BOOT INIT CAN BE REUSED':'新工作階段已準備','NEW SESSION - CHECKING CURRENT RESOURCES':'正在核驗資源並建立新工作階段','NEW SESSION COULD NOT START - SEE LOGS':'新工作階段未建立，請查看狀態','SESSION BUSY - WAIT FOR THE CURRENT ACTION':'工作階段忙碌中，請稍候','NEW JOB CREATED - OPEN SESSION STATUS':'新工作階段已建立','STOPPED - CHECK NXDEMO51/LOGS':'已停止，請查看狀態','BUSY - CHECK NXDEMO51/LOGS':'系統忙碌，請稍後再試',
 'Checking automatic initialization':'正在初始化並重顯影','Reuse live ready state or run original cold configuration':'核驗目前狀態並沿用原廠初始化流程','Auto configuration and JPEG complete':'初始化與JPEG輸出完成','Workspace and original EP reference returned':'工作空間與原廠參考已歸還',
 'NX LUT Lite':'NX LUT Lite','VIDEO':'影片設定','PHOTO':'照片','OFF / ORIGINAL':'關閉／原色','OFF':'關閉','ON':'開啟','WAIT':'請稍候','OK / APPLY':'OK 套用','OK / RESTORE':'OK 還原','READ STATUS':'讀取狀態','FULL / CONFIGURED':'全寬已設定','NATIVE / 4K':'原生4K','OTHER / UNKNOWN':'其他輸出','LOG DIRECT / RESTORE':'V‑Log／還原',
 'SRW LUT / FAVORITES':'SRW LUT／收藏','SRW LUT FAVORITES':'SRW LUT／收藏','APPLIED LUT SNAPSHOT - IDENTITY WHEN OFF':'選擇重顯影LUT，關閉時保留原色',
 'PROGRAM CHECK FAILED - INSTALL MATCHING PACKAGE':'程式版本不符，請安裝完整套件','CHECK CURRENT RESOURCES - NO REBOOT ASSUMED':'請核驗目前資源狀態','NEW SESSION UNCONFIRMED - SEE SESSION STATE / LOG':'尚未確認新工作階段，請查看狀態','RESET CHECK FINISHED - SEE SESSION STATE / LOG':'資源核驗結束，請查看狀態',
}
lines=['/* Display translation only; native protocols and diagnostic identifiers stay intact. */','#ifndef NXL_UI_I18N_H','#define NXL_UI_I18N_H','static const char*nxl_ui_text(const char*s){','static const struct{const char*a,*b;} map[]={']
for a,b in translations.items():lines.append('{'+json.dumps(a)+','+json.dumps(b,ensure_ascii=False)+'},')
lines+=['};for(unsigned i=0;i<sizeof map/sizeof*map;i++)if(!strcmp(s,map[i].a))return map[i].b;',
 'if(strchr(s,\'_\')&&strspn(s,"ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789")==strlen(s))return "操作未完成，請查看狀態";',
 'return s;}','#endif']
(U/'ui_i18n.h').write_text('\n'.join(lines)+'\n','utf8',newline='\n')

# Convert Chinese display literals/comments without altering ASCII wire keys.
for p in U.iterdir():
 if p.suffix in ['.c','.h','.inc']and p.name not in ['ui_font.h']:
  s=p.read_text('utf8');converted=trad(s)
  p.write_text(converted,'utf8',newline='\n')

profiles=json.loads((D/'looks-info.json').read_text('utf8'))
extra=[dict(id='',name='還原LUT',tone='關閉目前LUT，回到本模式基準。',scene='普通模式還原原廠；Log保留原曲線。',group='other'),
 dict(id='?',name='自訂濾鏡',tone='自訂LUT，請核對輸入曲線與色域。',scene='按OK開啟詳情，MENU逐級返回。',group='other'),
 dict(id='@video',name='影片LUT',tone='將目前LUT用於影片輸出。',scene='停止錄影後再切換或調整。',group='other')]
quote=lambda s:json.dumps(trad(s),ensure_ascii=False)
out=['/* Release catalogue; original sources and descriptions recorded together. */','#ifndef NXL_LOOK_INFO_H','#define NXL_LOOK_INFO_H','#define NXL_LOOK_INFO_VERSION "0.4.0-RC1"','struct nxl_look_info {const char*id,*name,*tone,*scene,*group;};']
for label,row in zip(['nxl_restore_info','nxl_unknown_info','nxl_video_info'],extra):out.append('static const struct nxl_look_info '+label+'={'+','.join(quote(row[k])for k in ['id','name','tone','scene','group'])+'};')
out.append('static const struct nxl_look_info nxl_look_infos[]={')
for row in profiles:out.append('{'+','.join(quote(row[k])for k in ['id','name','tone','scene','group'])+'},')
out+=['};','#endif'];(U/'look_info.h').write_text('\n'.join(out)+'\n','utf8',newline='\n')

original=json.loads((SRC/'base/nx-lut-lite/data/ui-glyphs.json').read_text('utf8'));glyphs=json.loads((D/'ui-glyphs.json').read_text('utf8'))
needed=set(chr(i)for i in range(32,127))
for p in U.iterdir():
 if p.suffix in ['.c','.h','.inc']and p.name!='ui_font.h':needed.update(c for c in p.read_text('utf8')if ord(c)>=128)
fontpath=W.parent/'dependencies/original-tooling/NotoSansCJKsc-Medium.otf';face=TTFont(fontpath);cmap=face.getBestCmap()
font={n:ImageFont.truetype(str(fontpath),n)for n in [23,24]}
missing=sorted(ord(c)for c in needed if str(ord(c))not in glyphs)
for cp in missing:
 assert cp in cmap,('font lacks Unicode character',cp,chr(cp))
 ch=chr(cp);cjk=cp>=0x3000;f=font[24 if cjk else 23];left=f.getbbox(ch,anchor='ls')[0]
 im=Image.new('L',(26,28));ImageDraw.Draw(im).text((1 if cjk else max(0,-left),24 if cjk else 22),ch,font=f,anchor='ls',fill=255)
 rows=[sum(1<<x for x in range(26)if im.getpixel((x,y))>=96)for y in range(28)]
 glyphs[str(cp)]=dict(advance=26 if cjk else min(26,math.ceil(f.getlength(ch))+max(0,-left)),rows=rows)
assert all(glyphs[k]==v for k,v in original.items())
put(D/'ui-glyphs.json',glyphs)
out=['/* Original bitmap glyphs retained; new Traditional characters use the same Noto Medium source. */','#ifndef NXL_UI_FONT_H','#define NXL_UI_FONT_H','struct nxl_ui_glyph{unsigned cp,advance;unsigned row[28];};','static const struct nxl_ui_glyph nxl_ui_font[]={']
for cp,g in sorted(glyphs.items(),key=lambda a:int(a[0])):out.append('{'+cp+','+str(g['advance'])+',{'+','.join(hex(r)for r in g['rows'])+'}},')
out+=['};','static const struct nxl_ui_glyph*nxl_ui_glyph(unsigned cp){unsigned lo=0,hi=sizeof nxl_ui_font/sizeof*nxl_ui_font;while(lo<hi){unsigned mid=(lo+hi)/2;if(nxl_ui_font[mid].cp<cp)lo=mid+1;else hi=mid;}if(lo<sizeof nxl_ui_font/sizeof*nxl_ui_font&&nxl_ui_font[lo].cp==cp)return nxl_ui_font+lo;return nxl_ui_glyph(63);}','#endif']
(U/'ui_font.h').write_text('\n'.join(out)+'\n','utf8',newline='\n')
put(W/'localization-validation.json',dict(passed=True,language='Traditional Chinese',original_glyphs=len(original),added_glyphs=len(missing),total_glyphs=len(glyphs),
 original_bitmaps_preserved=True,ASCII_protocol_fields_not_translated=True,font_source_sha256=sha(fontpath),catalogue_descriptions=len(profiles),
 native_unknown_reason_ids_mapped_to_readable_message=True,hardware_tested=False))
print('PASS Traditional display strings,',len(profiles),'descriptions and',len(glyphs),'bitmap glyphs',flush=True)

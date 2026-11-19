from context import *
import importlib.util,re,shutil,zipfile,time
needed=['math-and-flow-validation.json','UI-routing-validation.json','UI-visual-validation.json','touch-validation.json',
 'detail-async-cache-validation.json','native-aux-validation.json','engine-shell-validation.json','SRW-handoff-shell-validation.json',
 'SRW-completion-shell-validation.json','installer-validation.json','runtime-identity-validation.json','Panasonic-conversion-validation.json','LUT-quality-domain.json']
for n in needed:assert json.loads((W/n).read_text('utf8'))['passed'],n
ri_spec=importlib.util.spec_from_file_location('rc1_ri',W/'runtime_identity.py');ri=importlib.util.module_from_spec(ri_spec);ri_spec.loader.exec_module(ri);assert ri.audit(S)['passed']
sys.path.insert(0,str(R/'work'));from audit_camera_manifest import audit_package
assert audit_package(S,include_sums=False)['passed']
# Validate the exact inherited native baseline and the narrow Linux changes.
unchanged=[];changed=[];new=[]
for p in sorted((M/'bin').iterdir()):
 if not p.is_file():continue
 old=B/'nx-lut-demo/nx-lut-lite/bin'/p.name
 if old.exists():
  (unchanged if sha(p)==sha(old)else changed).append(p.name)
 else:new.append(p.name)
assert set(changed)=={'nxl-menu','nxl-session.arm'},changed
assert set(new)=={'nxl-lutblend.arm','nxl-sharp.arm','nxl-lutguard.arm','nxl-lutidle.arm'},new
assert sha(M/'native/libmmfcore.r11f.so')==sha(B/'nx-lut-demo/nx-lut-lite/native/libmmfcore.r11f.so')
for p in(T/'core/src').glob('*'):
 if p.is_file()and p.name not in ['nxl-lutguard.c','nxl-lutidle.c']:assert sha(p)==sha(SRC/'core/src'/p.name),p
for p in(T/'session/src').glob('*'):
 if p.is_file()and p.name not in ['session.c','progress_publish.inc']:assert sha(p)==sha(SRC/'session/src'/p.name),p
put(W/'baseline-invariance.json',dict(passed=True,unchanged_native_binaries=unchanged,changed_Linux_control_binaries=changed,new_helpers=new,
 original_Gamma_and_RTOs_source_unchanged=True,session_resource_and_journal_authority_unchanged=True,
 session_baseline_reproduced_byte_exact=True,core_library_unchanged=True,hardware_tested=False))
# All executable scripts must parse in the real production shell family.
for p in [S/'install.sh',S/'nx-lut-demo/deploy.sh',*M.glob('*.sh')]:
 r=subprocess.run([r'C:\Program Files\Git\usr\bin\sh.exe','-n',str(p)],capture_output=True,text=True);assert r.returncode==0,(p,r.stderr)
catalog=json.loads((M/'data/release-lut-catalog.json').read_text('utf8'))['looks'];assert len(catalog)==19
for r in catalog:
 assert r['tone']and r['scene']and not r['id'].endswith('_apx')
 assert sha(S/'looks'/(r['id']+'.bin'))==r['native_sha256']==sha(M/'looks'/(r['id']+'.bin'))
assert sum(x['input']=='STD'for x in catalog)==12 and sum(x['input']=='VLOG'for x in catalog)==7
report='''NX LUT Lite 0.4.0 RC1 驗證與交接報告

結果：候選包已完成電腦端驗證。相機內安裝與實拍驗收待使用者執行。
不把ARM指令模擬、腳本回放、桌面繪圖或SD讀回視為相機硬體已通過。

功能：R8f半屏詳情、四參數、M3雙強度混合、逐級返回與同次開機暫存；
繁體中文與統一五列佈局；SRW不顯示Gamma／線性／Log等研究開關。
普通LUT頁A／B和SRW永遠排除Log LUT，VID提供獨立的單張Log LUT入口。
SRW完成後先沿用既有資源核驗再建立空白新工作階段，不自動處理下一張。
保留所有舊日誌與原生資源憑據；新可選wrapper文字日誌合併為最新一份。

驗證：29組實際ARM M3運算，每組比較全部4913節點；42組真實生產shell控制流；
20組完成／新會話／日誌策略；實際ARM非同步回執、觸摸、輸入、參數暫存、
模式鎖與64KB進度記錄上限；8張以實際ARM繪圖函數產生的介面預覽。
另執行舊SRW reset_live_lut實際函數的鎖交接與松下新LUT模式專項。
從R11p原廠核心庫、R11q既有已修改核心庫各回放一次完整生產安裝；
兩者均保持核心庫位元組與inode不變、檔案系統恢復唯讀、使用者狀態和歷史保留。
模擬的邊界：OS／檔案／鎖／X11端點、相機普通RAM、Gamma租約回應與銳度原廠呼叫。
沒有在電腦上模擬硬體ISP、DMA、快取一致性、編碼器或實體相機核心。

本輪在寫卡前發現並修正：還原函數轉接、遺留SRW選擇、B暫存確認邊界、
安裝器要求的家族標記、SRW監督器run.lock與原有engine.lock還原交接的衝突。
SRW原本會在native工作前釋放engine.lock、還原LUT、再取得engine.lock；本包保留它。
新的LUT guard只持有video-mode.fd，沒有再次取得監督器run.lock，也沒有新增native權限。

19款LUT：原有3款真實表、EJAY膚色、使用者10張CUBE、官方V709、松下新增4款。
原版33／64／65點CUBE、ZIP、兩份作者說明與官方JSON元資料均保留。
Backlight-N=Natural；Smoky Color-S、Dramatic BlockBuster-S=Standard；
Dramatic BlockBuster=V-Log。根據API與CUBE的LUMIXPHOTOSTYLE聯合核對，未憑名稱猜測。
前3張屬普通分類，第4張屬VID的Log分類。未捏造Backlight／Smoky的Log版。
來源： https://panasonic.jp/dc/contents/lumix-color/real-time-lut.html
官方公開目錄： https://lumix-base.jpn.panasonic.com/lutapi/v2/community/luts/filter
官方詳情： https://lumix-base.jpn.panasonic.com/lutapi/v2/community/lut
官方資料下載： https://lumix-base.jpn.panasonic.com/lutapi/v2/community/luts/download
三方MIT程式僅用於理解公開接口，未執行它的程式碼或把其許可套用到LUT作品：
https://github.com/MangDao/LumixLutSearch
LUT權利仍歸原作者；這是使用者私人機身測試包，沒有發布到遠端倉庫。

機內17點8bit重採樣有誤差，不能聲稱等同原版高精度後期。全RGB與本機工作域的
數值報告分別留存；本機工作域是數學模型，不是新拍攝證據或物理灰卡標定。
V-Log沿用2.520358943倍工作曝光、V-Gamut轉換和已驗證的原廠回調；Gamma／RTOS
實際工作程序與R11q逐字節相同。核心庫、SRW顯影、鏡頭與JPEG工作程序未改動。
原始影片仍保留原廠709標籤，純Log後期需手動解釋V-Log/V-Gamut。
Log LUT會影響錄影寫卡，不宣稱原色監看與純Log寫卡分流已實現。

SD部署不等於相機已安裝。執行RC1_TEST_STEPS_zh.txt後才可核驗相機運行版本和畫面。
'''
(S/'RC1_VALIDATION_REPORT_zh.txt').write_text(report,'utf8',newline='\n')
source=S/'SOURCE/FORMAL_0_4_RC1';source.mkdir(parents=True,exist_ok=True)
for folder in ['base/nx-lut-lite/src','base/nx-lut-lite/vendor','base/nx-lut-lite/tools','base/nx-lut-lite/data','core/src','core/tools','core/tests','common','session/src','session/tools','session/tests','tools/log_video']:
 shutil.copytree(T/folder,source/folder,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
for p in (T/'base/nx-lut-lite').glob('*.sh'):shutil.copyfile(p,source/'base/nx-lut-lite'/p.name)
shutil.copytree(A,source/'release-tools',dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
shutil.copyfile(W/'runtime_identity.py',source/'runtime_identity.py')
(source/'installer').mkdir(exist_ok=True);shutil.copyfile(S/'nx-lut-demo/deploy.sh',source/'installer/deploy-rc1.sh');shutil.copyfile(S/'install.sh',source/'installer/install-entry.sh')
evidence=S/'EVIDENCE/RC1';evidence.mkdir(parents=True,exist_ok=True)
for n in needed+['baseline-invariance.json','SRW-handoff-review.json','Panasonic-conversion-validation.json','localization-validation.json','source-before.json']:
 shutil.copyfile(W/n,evidence/n)
shutil.copytree(W/'UI-preview',evidence/'UI-preview',dirs_exist_ok=True)
build=json.loads((S/'BUILD.json').read_text('utf8'));build['module_manifest_sha256']=sha(S/'nx-lut-demo/module.manifest');put(S/'BUILD.json',build)
# A formal ZIP must not contain inherited working logs/current-job pointers.
P=W/'release-payload';P.mkdir(exist_ok='--refresh'in sys.argv)
omitted=[]
for p in sorted(S.rglob('*')):
 if not p.is_file():continue
 rel=p.relative_to(S)
 if rel.parts[0]=='NXDEMO51':omitted.append(rel.as_posix());continue
 q=P/rel;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
put(W/'package-omissions.json',dict(passed=True,reason='never ship working logs or current session pointers into a user card',omitted=omitted))
(P/'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.relative_to(P).as_posix()+'\n'for p in sorted(P.rglob('*'))if p.is_file()and p.name!='SHA256SUMS'),'utf8',newline='\n')
assert ri.audit(P)['passed'];byteaudit=audit_package(P);put(W/'camera-byte-audit.json',byteaudit)
archive=W/'NX_LUT_Lite_0.4.0_RC1_full.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6)as z:
 for p in sorted(P.rglob('*')):
  if not p.is_file():continue
  name=p.relative_to(P).as_posix();info=zipfile.ZipInfo(name,time.gmtime(p.stat().st_mtime)[:6]);info.compress_type=zipfile.ZIP_DEFLATED
  info.create_system=3;executable=p.suffix in ['.sh','.arm']or p.name in ['nxl-menu','nxl-keys','nxl-fileops','patch-fileops']
  info.external_attr=((0o100755 if executable else 0o100644)<<16);z.writestr(info,p.read_bytes())
with zipfile.ZipFile(archive)as z:
 assert z.testzip()is None;assert len(z.namelist())==len([p for p in P.rglob('*')if p.is_file()]);assert not any(n.startswith('NXDEMO51/')for n in z.namelist())
put(W/'final-package-validation.json',dict(passed=True,release='0.4.0-RC1',payload=str(P),archive=str(archive),bytes=archive.stat().st_size,sha256=sha(archive),
 files=len([p for p in P.rglob('*')if p.is_file()]),module_manifest_sha256=sha(P/'nx-lut-demo/module.manifest'),checks=needed,
 native_invariance=True,camera_text_LF_only=True,archive_CRC_passed=True,working_logs_excluded=True,hardware_tested=False))
outputs=W.parent/'outputs';outputs.mkdir(exist_ok=True)
for f in [archive,S/'RC1_TEST_STEPS_zh.txt',S/'RC1_VALIDATION_REPORT_zh.txt',S/'RC1_LUT_CATALOG_zh.txt',W/'final-package-validation.json']:
 shutil.copyfile(f,outputs/f.name);assert sha(outputs/f.name)==sha(f)
print('PASS final ZIP, identities, raw LF byte audit, native baseline, 19 LUTs and full source',archive,flush=True)

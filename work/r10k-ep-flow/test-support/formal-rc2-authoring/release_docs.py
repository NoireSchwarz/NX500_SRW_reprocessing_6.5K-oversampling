from context import *
readme='''NX LUT Lite 0.4.0 RC2（NX500 1.12）

本包修正LUT選擇器短暫出現後消失，並精簡SRW、VID頁面。
電腦端驗證與相機驗收分開記錄；SD部署不等於相機已安裝／運行。
不更換核心庫、RTOS、Gamma、SRW顯影或JPEG原生工作程序。

LUT：進入半屏詳情僅載入本次開機參數，不因進入界面立即套用。
調整A參數仍會依既有防抖機制套用；套用時保持窗口可見，等待完成再編輯。
EV選B→OK調B→OK準備混合→返回A再OK提交。MENU逐級返回。
同次開機保留參數，冷啟動不自動套用。焦點刷新不視為確認／取消。

SRW：五項同頁，依序為原檔、LUT、工作狀態、初始化並重顯影、恢復相機。
沒有新工作階段按鈕；每次按初始化並重顯影，自動核驗上一輪資源並準備本輪。
完成後保留COMPLETE。下一次按初始化才準備下一輪，不在完成後啟動背景新任務。
未確認資源歸還則停止，不繼續顯影。Gamma、線性、Log等研究開關仍隱藏。
新可選wrapper文字日誌沿用最新一份覆蓋策略；保留原生資源憑據及所有歷史記錄。

VID：四項同頁：輸出狀態、畫面模式、V-Log開關、Log LUT。
畫面模式二級菜單：全寬UHD 24p、UHD轉2.5K、恢復原廠UHD。
原生裁切UHD30p測試項不顯示。上下鍵／下撥輪選擇，OK套用，MENU返回一級。
停錄並回照片待機後切換。RC2仍沿用原廠重選視頻規格的載入步驟，未啟用自動重載。

普通LUT頁A／B及SRW永遠排除Log LUT；Log LUT僅在VID入口使用單張A。
LUT共19張：12張普通輸入、7張V-Log輸入，介紹與原始CUBE沿用RC1。
Log LUT同時影響取景和寫卡，並非純監看還原。純Log後期手動指定V-Log/V-Gamut。
影片仍帶原廠709標籤；V-Log工作曝光2.520358943倍，未宣稱物理18%灰卡標定。
機內17點8bit有重採樣誤差，高精度後期優先使用PC_LUT_SOURCES／PC_VLOG_LUTs原版。

安裝與最小短測：RC2_TEST_STEPS_zh.txt。
視頻規格免進菜單重載研究：RC2_VIDEO_RELOAD_RESEARCH_zh.txt。
照片、影片、原始固件、完整備份、用戶設定及歷史日誌保留，不格式化、不修復文件系統。
'''
steps='''NX LUT Lite 0.4.0 RC2 合併短測

一、安裝
1. 電腦寫卡完成後安全移除。相機正常關機、寫卡燈熄滅後取電池約30秒再裝回。
   這可清除舊Gamma租約；不要跳過既有租約保護。依原有SD安裝入口執行一次安裝。
2. 安裝成功後完整關機再開機、斷開USB，依既有EV+AEL方式開啟選單。
3. VID頁顯示「NX LUT Lite 0.4.0 RC2」。卡上install.log應有
   install_revision=release-0.4.0-rc2和INSTALL_COMPLETE SUCCESS。
   寫入SD的檔案與校驗通過，不能替代以上相機實際運行確認。

二、同次開機驗證界面及SRW，不需要新增拍攝
1. 普通LUT頁任選一張，按OK進入下半屏詳情，先不操作等至少3秒。
   應保持可見；調5%強度或拖滑杆，套用期間也應可見，完成後可繼續編輯。
2. EV進B列表→OK調B→OK返回A準備混合→A中OK提交；MENU每次只退一級。
   若仍自行消失，停止重複嘗試，保留本輪記錄，說明消失前最後一次按鍵即可。
3. 還原LUT。SRW頁應只有五項，無「新工作階段」，無Gamma／線性／Log／測試項。
   選已知可用的真實SRW、選普通LUT或關閉，再按「初始化並重顯影」。
4. 等到JPEG已儲存、狀態COMPLETE，再按同一初始化按鈕處理第二輪。
   應自動核驗並接續，不需手動建立新階段；兩輪輸出都保留。
   若資源核驗失敗則停止，不連點、不強制清鎖。

三、VID排布與二級畫面模式
1. VID主頁四項同頁，不需翻頁。「畫面模式」OK進入三項列表。
2. 用上下鍵／下撥輪和觸摸切換游標；MENU返回時不套用。
   三項應為「全寬UHD 24p／UHD轉2.5K／恢復原廠UHD」，無裁切30p測試項。
3. 若需驗證套用，只測日常使用的一個模式即可，不重做碼率對照。
   本包沒有改視頻設定工作程序，仍需沿用既有原廠規格重選步驟：
   全寬先用NTSC／原生4K24p／PRO，套用全寬後再重選一次同一原廠規格。
   2.5K／還原UHD沿用既有UHD規格重選步驟。自動重載研究未混入RC2。
4. 停錄並確認所有寫卡結束後再接USB。保留兩輪SRW輸出和安裝日誌即可核驗本次修復。
'''
research='''NX500 視頻規格免進原廠菜單重載研究（RC2，電腦端靜態證據）

結論：原廠確實存在「更新錄像屬性→待機重建分辨率」的非菜單繪圖工作路徑。
有望免除手動重選，但尚未驗證相同枚舉值是否強制重建、事件傳送方式及實機完成條件。
不能僅憑函數名宣稱已能安全自動重載。RC2只修界面，未加入新native呼叫。

一、當前為何要重選
現有video-mode.sh先由nxl-video-mode修正進程RAM中的尺寸構造映射，再套用既有
全寬RTOS設定／原生24p映射；不重新執行原廠活動camcorder的屬性更新。
因此「構造指令已改」和「當前對象已用新尺寸建好」不是同一件事。
目前status回讀構造映射，不能單憑它證明當前錄像對象已重載。

二、原廠1.12實際調用鏈（本地platform.img提取）
di-camera-app SHA256：55dcc78143724435a2e38b9e0d809960bd728668634bbcf7532ce137f882eb04。
UI_Set_Attr_Movie_Size(eUS_MOVIE_SIZE)：0x0e4398，長488字節。
它按枚舉計算寬高、FPS及音視頻參數，經0x0e456c調用
UI_Mm_Camcorder_Attr_Movie_Info，地址0x1d7174。
原廠菜單提交鏈在0x286c2c讀當前0x109項，0x286c34調用上述尺寸屬性函數，
緊接0x286c38調用UI_Mm_Camcorder_Record_Stby_Set_Resolution（0x1d7090）。
其他已找到的同序列：0x2866b8/0x2866bc、0x287728/0x28772c。
0x1d7090從0x4aa288取得當前camcorder句柄，0x1d7160轉入
mmf_camcorder_record_stby_set_resolution。這是重建待機分辨率的具體候選入口。
不能只調後者而省略前面的屬性配置、模式及事件線程條件。

三、命令和事件線索，不能當作已驗證接口
原廠含「resolution [show,name]: set video record resolution」、
「SYSEVT_CAMERA_SET_MOVIE_RESOLUTION」及「rvf_set_movie_resolution」等字串。
命令處理候選位於0x2779dc；0x2779ec讀0x27a狀態並要求為1，否則輸出
「NOT IN LIVEVIEW STATE」錯誤。它是原廠測試命令路徑，不等於生產用快捷入口。
本輪尚未核實可用命令前綴、完整參數表與事件編號，未向相機發出猜測命令。
onEventSetMovie（0x2e8b44）只清圖標／畫錄像圖標，不能因名稱就當規格重載。
UI_Restart_Movie_Standby也不能當無條件刷新使用。

四、後續最小實作方向
優先沿已存在的原廠事件隊列重放「當前尺寸屬性＋待機分辨率重建」提交，
而非另開線程直接調ISP／MMIO／RTOS，亦不替換核心庫。
先核實事件結構與回調線程、當前枚舉和生效尺寸；只在停錄、無寫卡、無SRW任務、
狀態可確認時執行。必須等待原廠完成回應並回讀實際活動輸出，超時保留手動重選路徑。
下一個獨立測試只需日常全寬24p與恢復的一組短錄，不做碼率測試；檢查
無重選即可切換、MP4實際尺寸／時間基和回退。2.5K另核對其前置原廠規格。

證據：EVIDENCE/RC2/video-reload-research 下的固件SHA、符號、反匯編、調用者清單。
這是原廠機器碼靜態證據與方案，不是已完成的實機自動重載測試。
'''
validation='''NX LUT Lite 0.4.0 RC2 驗證報告

修復：LUT詳情進入時不再排入280ms後自動套用；套用期間保持窗口可見，
不再透過Unmap/Map與反覆搶焦點重開窗口。受控二級菜單忽略焦點刷新，
確認／返回仍由按鍵與觸摸決定。相機上是否完全消除現象待短測確認。

SRW：移除新工作階段菜單，五項同頁。資源核驗與新工作階段在下一次明確
按初始化按鈕時完成，保留既有session權限、原生EP／鏡頭／JPEG工作程序。
VID：四項同頁，新增三項畫面模式二級選擇器，移除裁切30p測試入口。

驗證範圍：實際ARM菜單函數的3秒空閒、焦點、參數更新、非同步完成、
五項SRW／四項VID／三條模式派發、按鍵／觸摸與逐級返回；
16組實際auto-init shell控制流程（正常、COMPLETE／RESTART_REQUIRED接續、
已用新階段、核驗失敗、狀態改變、顯影失敗、缺歸還憑據、未提交COMPLETE）。
8張實際ARM繪圖函數與實際字模的界面預覽；實際生產安裝器升級回放；
全包hash綁定、ARM／shell格式與ZIP CRC檢查。詳見EVIDENCE/RC2各JSON。
OS、X11、資源端點與原生工作程序回應在電腦驗證中建模，沒有代替實體硬件驗收。

除Linux菜單外的所有native可執行檔、核心庫、Gamma／全寬RTOS程序、LUT數據
相對RC1逐字節相同。其他shell只允許依賴hash重綁，功能變更僅auto-init。
原有19張LUT的來源、權利、8bit／17點誤差與V-Log工作標度沿用RC1報告。
本包不重複碼率測試，不聲稱改善物理幀率、曝光或色彩標定。

卡上本次檢查已見RC1 INSTALL_COMPLETE SUCCESS和核心庫未變，說明本次UI問題
不能直接歸因於未安裝RC1。RC2需安裝完成及短測後另核驗實際運行版本。
保留照片、錄像、完整備份、歷史日誌與用戶設定，不執行清理／格式化／修復。
'''
for name,text in [('README_zh.md',readme),('RC2_TEST_STEPS_zh.txt',steps),('RC2_VALIDATION_REPORT_zh.txt',validation),('RC2_VIDEO_RELOAD_RESEARCH_zh.txt',research)]:
 (S/name).write_text(text,'utf8',newline='\n')
(M/'README_zh.md').write_text(readme,'utf8',newline='\n')
build=json.loads((S/'BUILD.json').read_text('utf8'));build.update(format='NXL_RELEASE_RC2',release='0.4.0-RC2',runtime_revision='release-0.4.0-rc2',SRW_rows=5,VID_rows=4,video_mode_picker_routes=3,video_auto_reload_enabled=False)
put(S/'BUILD.json',build)
print('PASS current RC2 docs and reload findings',flush=True)

from context import *
readme='''NX LUT Lite 0.4.0 RC3（NX500 1.12）

二級菜單臨時獨占兩個按鍵事件設備，防止選LUT或調參時同時改變原廠曝光／光圈。
返回主頁即釋放，子進程關閉重複描述符而不解除父進程的獨占。
不能取得完整獨占時不進入二級菜單，顯示提示；不修改原廠按鍵遮罩、不注入快門事件。
半按快門可退出；若需要原廠對焦，鬆開後再半按。冷啟動不自動套用LUT。

切換V-Log會自動清除上一模式LUT，無需手動逐個還原：
開啟：停錄／照片待機核驗→原有restore_factory交易→純V-Log基準→原有Gamma要求。
關閉：優先還原Gamma→核驗原廠狀態→清除Log LUT／普通LUT並恢復原廠LUT及指針。
有LUT但無Log標記時也會恢復。失敗保留回復資料，不顯示假成功；菜單重新讀取已提交狀態。
純V-Log啟用後仍可從VID專用入口另選Log LUT，它會同時影響取景和寫卡。

SRW保留RC2五項同頁與一鍵初始化並重顯影；VID保留四項同頁和三種畫面模式二級菜單。
普通LUT頁A／B與SRW永遠排除Log LUT。M3混合與5%強度調整規則不變。
視頻規格自動重載仍未啟用；全寬／2.5K切換沿用原廠規格重選步驟。

共32張LUT：25張普通輸入、7張V-Log輸入，全部附有介紹。
從looks.rar新增11張普通表，與現有3張重複基準逐字節核對，原生表不再次轉換。
跳過附件的膚色、5207、Gold200相關與舊_apx模擬項；不刪除先前已安裝的正式表。
另加入XH 5219明亮（3）與哈蘇風格（7），使用者確認為普通／標準畫面輸入。
XH作品非哈蘇原廠標定；原版33點CUBE保留，機內為17點8bit數值重採樣。
詳細介紹見RC3_LUT_CATALOG_zh.txt；原始資料與來源保留在PC_LUT_SOURCES。

核心庫、RTOS、原生Gamma、SRW顯影／鏡頭／JPEG、M3運算與既有19張LUT均保持RC2基線。
V-Log沿用2.520358943倍工作曝光，未宣稱物理灰卡標定。純Log影片標籤仍為原廠709，
後期需手動解釋V-Log/V-Gamut；不要對已帶Log LUT的風格輸出重複套還原。

安裝前正常關機、燈熄後取電池約30秒再裝回；詳見RC3_TEST_STEPS_zh.txt。
電腦測試與SD讀回不是相機實機驗收。照片、錄像、完整備份及歷史日誌保留。
'''
steps='''NX LUT Lite 0.4.0 RC3 合併短測（不重複碼率測試）

1. 安全移除SD／斷開USB。正常關機、等寫卡燈熄，取出電池約30秒再裝回。
   按原SD安裝入口安裝；成功後完整關機再開機，斷開USB，開EV+AEL選單。
   VID顯示0.4.0 RC3；實際安裝需核對install.log中release-0.4.0-rc3和INSTALL_COMPLETE SUCCESS。

2. 固定ISO／快門／光圈，記住原廠顯示值。普通LUT按OK進二級詳情，等待3秒。
   轉上下撥輪、方向鍵與觸摸，確認LUT／參數改變但原廠曝光值不變。
   EV進B→OK調B→OK準備→返回A再OK提交；MENU每次只退一級。
   返回主頁／退出後，原廠波輪應恢復正常。半按退出後可鬆開再半按，無快門注入。
   若顯示無法獨占按鍵，先退出其他按鍵工具，保留記錄，不繼續串鍵操作。

3. 人像分類可找到「XH 5219 明亮」和「XH 5219 哈蘇風格」，上下撥輪及觸摸選擇。
   兩張均為普通輸入；使用相同曝光、白平衡比較。其他新增表見LUT目錄。
   欲在SRW選擇時，先在普通LUT頁收藏。SRW／普通A/B不應出現任何Log LUT。

4. 停錄回照片待機，先套一張普通LUT，再直接在VID啟用V-Log，不手動還原LUT。
   應自動清除原LUT，進影片取景等待原廠Gamma更新，錄約5秒純Log。
5. 停錄回照片待機，從VID→Log LUT選V709或另一張，再錄約5秒。
6. 停錄回照片待機，直接關閉V-Log，不先手動關Log LUT；進影片取景再錄約5秒原廠。
   應恢復原廠曲線與LUT；回普通LUT頁應可直接繼續編輯，不誤報影片LUT仍開啟。
   Gamma如顯示等待原廠更新，不能把它當已生效；按既有方式進影片取景觸發原廠更新。

7. 所有保存結束再接USB，保留三段影片與安裝日誌。核心庫／ISP／SRW沒有新增工作程序，
   無需重做歷史顯影實驗或碼率測試。若遇失敗，保留畫面提示與本輪記錄再定位。
'''
report='''NX LUT Lite 0.4.0 RC3 驗證報告

問題與修正：RC2二級菜單讀取原始事件但未獨占，半屏窗口不能阻止原廠同時收到波輪。
RC3僅在二級LUT、B窗口、SRW LUT及畫面模式選擇器存續時使用EVIOCGRAB；
返回即釋放。部分設備取得失敗會撤銷已取得部分，不能獨占則拒絕開啟二級界面。
子進程關閉自己的重複FD，不發UNGRAB；進程結束由Linux釋放所屬開啟文件的獨占。
Linux輸入接口依據：https://docs.kernel.org/driver-api/input.html （input_grab_device）。
相機內核／驅動的實際行為仍待短測，未用原廠KeyMask、MMIO或事件注入來掩蓋問題。

Log：以已有原廠LUT還原交易，在啟用前清理舊LUT並建立純Log基準；關閉後無論
Log policy標記是否存在都核驗並恢復LUT。Gamma恢復仍優先於可選LUT引擎校驗。
新菜單刷新讀取RAM提交狀態，避免舊video_enabled／current快取造成不必要的手動清理。
不自動套V709；開啟後是純V-Log，關閉後是原廠輸出。

電腦端證據：實際ARM輸入交易與失敗回退；120次雙路波輪派發建模；4組已提交LUT
狀態刷新；實際ARM二級菜單／焦點／按鍵／觸摸回歸；22組實際生產shell Log切換、
失敗及接續，使用實際ARM表讀寫與M3；最終32張表通過原生格式與校驗；
兩張33點XH CUBE轉換與每張8192個RGB點數值比較；安裝回放、全包SHA及ZIP CRC。
OS、evdev、X11、Gamma回應、相機普通RAM及銳度回應被建模，沒有以此代替實機驗收。
本輪只編譯Linux菜單；其餘native程序及核心庫與RC2逐字節相同。

導入11張已轉換表及2張XH，保留原始RAR／CUBE和作者版權字段；僅作使用者私人測試包。
舊預製25/50/75%強度檔不啟用，沿用M3連續5%調整。排除舊_apx與指定類型。
XH重採樣不是無損：明亮版MAE約0.93碼、哈蘇風格版約1.26碼；高飽和邊緣最差可更大。
這是電腦模型誤差，不是實拍色差。機內17點8bit限制與既有V-Log工作標度仍存在。

完整證據見EVIDENCE/RC3各JSON；安裝部署不等於相機實際安裝成功。
照片、錄像、原始固件、完整備份、歷史日誌和使用者狀態不作清理或替換。
'''
for name,s in [('README_zh.md',readme),('RC3_TEST_STEPS_zh.txt',steps),('RC3_VALIDATION_REPORT_zh.txt',report)]:
 (S/name).write_text(s,'utf8',newline='\n')
(M/'README_zh.md').write_text(readme,'utf8',newline='\n')
catalog=json.loads((M/'data/release-lut-catalog.json').read_text('utf8'))['looks']
description='NX LUT Lite RC3 LUT目錄：25張普通／7張V-Log\n\n普通A／B及SRW不含Log LUT；Log LUT僅VID。\n\n'
for row in catalog:description+=row['name']+' ['+row['input']+']\n'+row['tone']+'\n'+row['scene']+'\n\n'
(S/'RC3_LUT_CATALOG_zh.txt').write_text(description,'utf8',newline='\n')
build=json.loads((S/'BUILD.json').read_text('utf8'));build.update(format='NXL_RELEASE_RC3',release='0.4.0-RC3',runtime_revision='release-0.4.0-rc3',ordinary_LUT_count=25,Log_LUT_count=7,
 temporary_modal_input_ownership=True,Log_transition_clears_previous_LUT=True,video_auto_reload_enabled=False)
put(S/'BUILD.json',build)
print('PASS RC3 docs and 32 consistent introductions',flush=True)

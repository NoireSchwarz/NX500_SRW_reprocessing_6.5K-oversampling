from context import *
steps='''NX LUT Lite 0.4.0 RC4 合併短測

安裝
1. 安全退出電腦上的 SD 卡／USB；關機待寫卡燈熄滅。
2. 取出電池約30秒，再裝回開機完成 RC4 安裝。完成後再完整關機開機。
3. EV+AEL → VID，確認標題為 0.4.0 RC4。不要在錄影或 SRW 工作期間切換。

同一次開機，先做以下操作；選擇、移動滑桿都只暫存，OK才裝載。
1. 在 LUT 頁選一張容易辨認的普通 LUT，OK進入下半屏；改選或改參數後先等1秒，畫面不應立即改變。OK後等「已裝載」，上半屏應直接更新，下半屏保持開啟；無須半按才生效。再按半快門才返回取景。
2. 在取景連按3次EV（相鄰間隔約0.1–0.4秒），應回到剛才已應用的 LUT 與參數。A/B已提交的混合也應保留。單按EV仍是原有收藏／混合功能，稍延後判定。
3. VID → V-Log開關，OK啟用並等完成，應自動恢復純Log基準並更新取景，菜單留在原頁。VID → Log LUT → 選一張、OK裝載；再三按EV應跟到當前Log LUT。選「純Log」並OK可移除創意Log LUT。普通 LUT 頁與 SRW 仍不能選Log LUT。
4. VID → V-Log開關，OK還原，等完成。確認原廠畫面及普通LUT選擇恢復。若顯示未確認／等待，先不要錄影，保留記錄。

視頻規格（不用重新進原廠菜單選視頻尺寸）
先保留原廠 NTSC、PRO；僅需最初設好一次。停錄並回到照片待機。
5. VID → 畫面模式 → 全寬UHD24p → OK，等「規格已重新提交」，菜單應仍開啟；半按返回取景後直接錄5秒。
6. 回照片待機，選 UHD轉2.5K → OK；不重選原廠視頻尺寸，直接錄5秒。
7. 回照片待機，選 恢復原廠UHD → OK；直接錄5秒。
預期分別為3840×2160原生24p、2560×1440、3840×2160原生30p路徑。最後以MP4尺寸／幀率和畫面範圍核驗，不把配置回讀當成硬體實測。

不需要再測碼率。完成並停錄、確認寫卡燈熄滅後再接USB。
若只做最快驗收，先完成1–4和5；其餘兩條畫面模式可隨後驗收。
不清除照片、影片、完整備份或歷史日誌。PC測試不代表相機硬體／內核／取景無閃爍已通過。
'''
report='''NX LUT Lite 0.4.0 RC4 實作與驗證說明

本次改動
普通LUT、VID專用Log LUT、Log開關加入確認後一次性即時裝載。按鍵／觸控只改草稿；OK提交。套用過程不卸載菜單，半按快門仍負責返回取景。保留普通A/B混合原有兩次OK提交流程。
三按EV採用400ms最大按住時間、420ms相鄰間隔；自動重複不算額外次數，其他按鍵與組合鍵會中止連按。現有EV+AEL與EV+WiFi優先。普通單按EV延至連按窗口結束，或下一個其他操作前執行。
快捷入口按已應用的運行狀態及完整參數收據恢復；不把未提交草稿當作現行設定。增加原廠／純Log基準項。裝載失敗保留pending，不發成功收據。

Metol路徑
依據 WApaperplane/metol 固定提交 b10ec8413ce5f9a03079288cb9880e4d0fac983f 的 nx_lutload.c；只移植單次取景裝載，不移植影片監看器，也不替換現有DYN24視頻指標路徑。
取景CORE RSZ_OUT1_EOF(14)同步後，執行完整3DLUT寄存器順序，等待ACC RD_FINISH(7)，隨即退出Access狀態，再輸出日誌。等待逾時／錯誤也清除Access。只接收6個已知原廠／工作表地址。原廠恢復使用本次工作前捕獲的來源地址；不猜測白平衡、曝光或矩陣。
IRQ完成與寄存器回讀只能證明傳輸得到確認；不能獨立證明取景畫質、無閃爍或新片起始幀正確。

視頻規格
按原廠 di-camera-app 的 record resolution 24f／30f 命令重新提交規格。原函數 cmd_set_menu_option 使用GUI事件5／4，並由 nx_cmd_update_gui 執行原廠S1更新；並非我們直接寫輸入裝置。菜單在該操作期間不因S1離開。未使用切換P/M曝光模式、拍照或錄影開始／停止命令。
只讀核驗固件／原廠st工具身份、唯一相機進程及啟動時間、原廠共享UserSpec映射、NTSC、PRO及目標尺寸項；命令自身逾時只終止本工具啟動的子進程。新增工具不使用ptrace、不直接寫相機進程記憶體。原有全寬／尺寸RAM補丁保持原實現。
配置與原廠命令提交核驗通過後，真實輸出仍必須用新MP4檢查。不能據此宣稱實機規格已驗收。

Log開關
保留已驗證的原廠Gamma/CC入口與原RTOS程式。準備／還原期間暫緩3DLUT傳輸；讀取Gamma queued epoch確認原廠已排隊後，只做一次最後裝載。若等待原廠回調，最多補送一次同尺寸的原廠照片resolution命令，再有限等待；未確認則失敗，不假報立即生效。原廠Gamma恢復優先於選用LUT引擎身份檢查。

回歸邊界
SRW的reset_live_lut保持原本的表／指標還原、engine.lock交接；明確跳過新的EP裝載，避免重引入EP in use。SRW原生管線、核心庫、Gamma/RTOS二進位及32張LUT內容維持RC3。照片／影片／備份／日誌不作清理。
詳見EVIDENCE/RC4中的真實ARM控制流、真實shell及安裝器回放結果；所有MMIO、IRQ、原廠命令和OS端點的模擬邊界均有標註。相機尚待合併短測。

原始依據：https://github.com/WApaperplane/metol/blob/b10ec8413ce5f9a03079288cb9880e4d0fac983f/test_server/lutpipe/nx_lutload.c
'''
for name,text in [('RC4_TEST_STEPS_zh.txt',steps),('RC4_VALIDATION_REPORT_zh.txt',report),('RC4_RELOAD_RESEARCH_zh.txt',report)]:
 (S/name).write_text(text,'utf8',newline='\n')
p=M/'README_zh.md';p.write_text('NX LUT Lite 0.4.0 RC4\n\n'+steps+'\n'+report,'utf8',newline='\n')
p=S/'README_zh.md';p.write_text('NX LUT Lite 0.4.0 RC4\n\n請閱讀 RC4_TEST_STEPS_zh.txt 及 RC4_VALIDATION_REPORT_zh.txt。\n\n'+steps,'utf8',newline='\n')
build=json.loads((S/'BUILD.json').read_text('utf8'));build.update(release='0.4.0-RC4',live_LUT_reload='Metol EOF14 DMA7 one-shot',apply_policy='OK commit, menu remains mapped',EV_shortcut='three taps, applied state',video_spec_reload='OEM command plus read-only acknowledgement; hardware pending',hardware_tested=False);put(S/'BUILD.json',build)
print('PASS RC4 user steps and evidence boundaries')

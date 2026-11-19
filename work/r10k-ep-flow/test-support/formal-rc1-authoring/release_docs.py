from context import *
import shutil
catalog=json.loads((M/'data/release-lut-catalog.json').read_text('utf8'))['looks'];assert len(catalog)==19
readme='''NX LUT Lite 0.4.0 RC1（NX500 1.12）

這是正式功能候選包，電腦端驗證與相機驗收分開記錄。
以已驗證的SRW重顯影、全寬UHD 24p及V-Log/V-Gamut路徑為基線。
核心庫、RTOS程式、SRW解碼／顯影／鏡頭校正／JPEG原生工作程序不變。

LUT：下半屏詳情，四項參數、EV選B與M3雙強度混合。
上撥輪選分類；左右鍵切換功能欄／內容欄；上下鍵與下撥輪移動目前焦點。
MENU逐級返回。同次開機保留每張LUT的參數；冷啟動不自動套用。

SRW：僅顯示原檔、LUT、狀態、初始化並重顯影、新工作階段、恢復相機。
Gamma、線性、Log、鏡頭診斷與其他測試選項均不顯示，研究接口保留。
處理完成後沿用既有資源核驗建立空白新工作階段，不自動開始下一張。
未確認資源歸還時不宣稱新工作階段成功；已保存JPEG不受影響。
新可選文字日誌覆蓋為最新一份；舊日誌、原生資源憑據與所有照片／影片保留。

V-Log：先停止錄影並回照片待機，再從VID啟用或還原。
Log LUT僅由VID專用入口選擇；普通LUT頁A／B及SRW永遠排除Log LUT。
Log只使用單張專用LUT；普通模式使用M3並行混合，不能串接跨輸入域的B。
Log LUT會影響影片取景及寫卡，並非僅監看還原；純Log請選普通LUT頁的還原LUT。
機內原片仍保留原廠709標籤。純Log後期需手動指定V-Log/V-Gamut，避免重複還原。
本機工作曝光沿用2.520358943倍；沒有宣稱實體18%灰卡標定或增加感光元件動態範圍。

共19張有效LUT：12張普通輸入、7張V-Log輸入，皆附介紹。
原版CUBE與來源見PC_LUT_SOURCES；高精度後期優先使用原版。
機內17點、8bit表有重採樣誤差；不等同原版33／64／65點無損映射。
Backlight-N基於松下Natural；Smoky Color-S與Dramatic BlockBuster-S基於Standard。
它們在NX500普通畫面上可試用，沒有宣稱松下Natural與三星原廠風格相同。
另含真正V-Log輸入的Dramatic BlockBuster，放在VID的Log LUT。
LUT作品權利歸各創作者；公開免費下載不等於以開源許可授權整個作品庫。

安裝和一次合併驗收見RC1_TEST_STEPS_zh.txt。未進行實機驗收前保留RC標記。
'''
(S/'README_zh.md').write_text(readme,'utf8',newline='\n');(M/'README_zh.md').write_text(readme,'utf8',newline='\n');(T/'base/nx-lut-lite/README_zh.md').write_text(readme,'utf8',newline='\n')
steps='''NX LUT Lite 0.4.0 RC1 合併驗收（先短後長，無碼率測試）

一、安裝
1. 電腦寫卡完成後安全移除。相機正常關機、燈熄後取電池約30秒，再裝回。
2. 按既有原廠SD安裝入口執行一次安裝。此包不激活／替換核心庫。
3. 安裝成功後完整關機再開機，斷開USB，開藍牙後用EV+AEL呼出選單。
4. VID頁應顯示NX LUT Lite 0.4.0 RC1。安裝成功需以卡上install.log的
   install_revision=release-0.4.0-rc1及INSTALL_COMPLETE SUCCESS核驗。
   電腦已部署和讀回通過，仍不等於相機已安裝／運行。

二、同一次開機先做菜單與SRW短測
1. 確認左鍵進功能欄、右鍵回內容；上下鍵／下撥輪只移動目前焦點；
   LUT頁上撥輪可換分類。VID分頁應與另外兩頁同寬同高，無測試開關。
2. 普通LUT頁挑一張，EV加入收藏。OK進半屏A；試5%強度、色相、銳度、對比、
   觸摸頁簽和拖動。EV選B→OK調B→OK準備混合→回A再OK提交。
   A/B各80%會按比例歸一，A/B均0%應回到原廠基準；MENU每次只退一級。
3. 回同一張LUT應保留本次開機參數。退出詳情後先用還原LUT回原廠。
4. SRW頁確認沒有Gamma／線性／Log／AUTO+COMPARE／鏡頭診斷項。
   選一張已知可用的真實SRW、選收藏普通LUT，再按「初始化並重顯影」。
   等待JPEG已儲存且新工作階段準備完成。第二次可再用同一張原檔驗證接續，
   不需要拍攝合成SRW。若提示新會話未確認，保留已存JPEG與記錄，停止重試。

三、V-Log與新LUT（停錄、照片待機時切換）
1. VID啟用V-Log，退出選單進影片取景，先錄一段約5秒純Log。
2. 停錄並回照片待機，VID→Log LUT選「V-Log → V709」或Dramatic BlockBuster，
   錄第二段約5秒。它會同時改變取景和寫卡，不要後期再套一次還原。
3. 停錄回照片待機，在普通LUT頁／SRW列表確認看不到任何Log LUT。
4. VID還原原廠曲線後，可用普通LUT頁的Backlight-N、Smoky Color-S或
   Dramatic BlockBuster-S；不可把這三張直接當V-Log還原表。
5. 最後還原LUT和原廠曲線，錄第三段約5秒原廠，檢查原色恢復。
6. 全部保存結束後再接USB。保留安裝日誌、三段影片和SRW輸出JPEG即可合併核對。

純Log後期：手動解釋為V-Log/V-Gamut。原片標籤仍是709；原版V709還原檔見PC_VLOG_LUTs。
有LUT的寫卡片段：它已是風格輸出；勿再作V-Log解碼。
'''
(S/'RC1_TEST_STEPS_zh.txt').write_text(steps,'utf8',newline='\n')
catalogtext='NX LUT Lite RC1 LUT目錄\n\n'
for r in catalog:
 catalogtext+=r['name']+' ['+r['input']+']\n'+r['tone']+'\n'+r['scene']+'\n來源SHA256：'+r['source_sha256']+'\n\n'
(S/'RC1_LUT_CATALOG_zh.txt').write_text(catalogtext,'utf8',newline='\n')
put(S/'BUILD.json',dict(format='NXL_RELEASE_RC1',release='0.4.0-RC1',runtime_revision='release-0.4.0-rc1',internal_session_protocol='0.4.0-demo51-422',
 menu_language='Traditional Chinese',ordinary_LUT_count=12,Log_LUT_count=7,Log_definition='Panasonic V-Log/V-Gamut',encoding_bits=8,
 working_exposure_gain=2.5203589432187403,physical_reflectance_calibrated=False,source_primaries_independently_certified=False,
 Log_LUT_scope='VID only, single A; never normal A/B or SRW',camera_colour_tags_changed=False,
 core_library_changed=False,RTOS_changed_from_R11q=False,SRW_native_pipeline_changed=False,physical_camera_tested=False))
print('PASS release metadata, Traditional instructions and 19 complete introductions',flush=True)

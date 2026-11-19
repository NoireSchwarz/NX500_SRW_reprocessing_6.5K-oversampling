/* Release catalogue; original sources and descriptions recorded together. */
#ifndef NXL_LOOK_INFO_H
#define NXL_LOOK_INFO_H
#define NXL_LOOK_INFO_VERSION "0.4.0-RC5"
struct nxl_look_info {const char*id,*name,*tone,*scene,*group;};
static const struct nxl_look_info nxl_restore_info={"","還原LUT","關閉目前LUT，回到本模式基準。","普通模式還原原廠；Log保留原曲線。","other"};
static const struct nxl_look_info nxl_unknown_info={"?","自訂濾鏡","自訂LUT，請核對輸入曲線與色域。","按OK開啟詳情，MENU逐級返回。","other"};
static const struct nxl_look_info nxl_video_info={"@video","影片LUT","將目前LUT用於影片輸出。","停止錄影後再切換或調整。","other"};
static const struct nxl_look_info nxl_look_infos[]={
{"fuji_provia100f","Fuji Provia 100F","自然均衡，色彩清晰。","適合旅行、風景與日常記錄。","landscape"},
{"kodak_portra400","Kodak Portra 400","柔和暖調，色彩較為克制。","適合人像、生活與柔和光線。","portrait"},
{"mono_hp5","Ilford HP5","經典黑白，明暗層次鮮明。","適合街拍、人像與紀實。","mono"},
{"ejay_skin_native","EJAY 膚色最佳化","選擇性調整紅橙色區，同色物件亦受影響。","保留其他色調；不含人臉識別或磨皮。","portrait"},
{"ejay_5207_n","5207 Noritsu","諾日士沖掃方向，暖調日光電影配色。","日光人像與街景；原表標準畫面輸入。","cinema"},
{"ejay_5207_n_fx","5207 Noritsu FX","諾日士配色加藍色增強，暗部去紅。","適合藍天與海景；固定合適白平衡比較。","cinema"},
{"ejay_5207_s","5207 SP3000","SP3000沖掃方向，原表帶青色修正。","日光電影配色；與Noritsu版本比較。","cinema"},
{"ejay_g200_fx_n","Gold 200 FX 暖","Gold 200配色，FX藍色增強與暖調版本。","作者用於普通／標準畫面；依使用者確認分類。","retro"},
{"ejay_g200_n_fx_l","Gold 200 Noritsu FX 冷","Gold 200諾日士配色，FX藍色增強與冷調版本。","作者用於普通／標準畫面；依使用者確認分類。","retro"},
{"vlog_ejay_5207_n","VLog 5207 Noritsu","諾日士沖掃方向的日光電影配色。","作者原版V‑Log LUT；僅從VID的Log入口選擇。","loglut"},
{"vlog_ejay_5207_n_fx","VLog 5207 Noritsu FX","諾日士配色加FX藍色增強。","作者原版V‑Log LUT；僅從VID的Log入口選擇。","loglut"},
{"vlog_ejay_5207_s","VLog 5207 SP3000","SP3000沖掃方向的5207配色。","作者原版V‑Log LUT；僅從VID的Log入口選擇。","loglut"},
{"vlog_ejay_g200_n_fx_l","VLog Gold 200 FX 冷","Gold 200諾日士配色，FX藍色增強與冷調。","作者原版V‑Log LUT；僅從VID的Log入口選擇。","loglut"},
{"vlog_ejay_g200_fx_n","VLog Gold 200 FX 暖","Gold 200配色，FX藍色增強與暖調。","作者原版V‑Log LUT；僅從VID的Log入口選擇。","loglut"},
{"vlog_v709","V‑Log → V709","松下官方V709還原，輸出展開為滿範圍。","V‑Log／V‑Gamut專用；17點機內重採樣版本。","loglut"},
{"pan_backlight_n","Backlight-N","自然暖調、柔化暗部對比，適合逆光人像。","原表基於松下Natural；本機普通畫面試用。","portrait"},
{"pan_smoky_s","Smoky Color-S","抑制亮部與暖色，呈現低飽和的煙霧質感。","原表基於Standard；適合建築、街景與藍天。","cinema"},
{"pan_dramatic_s","Dramatic BlockBuster-S","青橙對比的電影配色，保留鮮明的冷暖層次。","Kotaro Kumazawa；原表基於Standard。","cinema"},
{"vlog_pan_dramatic","VLog Dramatic BlockBuster","V‑Log專用青橙電影配色，帶膠片式對比。","Kotaro Kumazawa原版；僅從VID選擇。","loglut"},
{"film_cinestill","CineStill 電影風格","偏冷暗部配暖色亮部，帶電影膠片感。","適合夜景與街景；為色彩風格，非光暈或顆粒效果。","cinema"},
{"fuji_400h","Fuji Pro 400H","柔和色彩與清淡綠調，明暗對比較柔。","適合日光人像、花卉與生活記錄。","portrait"},
{"fuji_astia100f","Fuji Astia 100F","較柔和的正片色彩，保留細緻中間調。","適合人像、旅行與柔和日光。","portrait"},
{"fuji_classicchrome","Fuji Classic Chrome","降低飽和度並加強冷暖分離，紀實風格。","適合街拍、建築與陰天。","retro"},
{"fuji_eterna500","Fuji Eterna 500","低對比、較克制的電影色調。","適合影片與柔和光線；普通畫面輸入。","cinema"},
{"fuji_velvia50","Fuji Velvia 50","強烈飽和與對比，綠藍色較鮮明。","適合自然風景；高飽和色可能較易裁切。","landscape"},
{"kodak_2383","Kodak 2383","電影拷貝方向的色彩與較深暗部。","本檔沿用既有普通畫面轉換，不作Log輸入表。","cinema"},
{"kodak_ektar100","Kodak Ektar 100","鮮明色彩與清晰明暗，偏暖膠片風格。","適合日光風景、建築與旅行。","landscape"},
{"mono_acros","Acros 黑白","黑白映射，強調明暗分離與質感。","適合街拍、建築與紀實；不增加顆粒。","mono"},
{"pentax_satobi","Pentax Satobi","低飽和的暖調與褪色感。","適合懷舊街景、人像與生活場景。","retro"},
{"ricoh_retro","Ricoh Retro","偏暖、較低飽和的懷舊色彩。","適合旅行、街拍與日常記錄。","retro"},
{"xh_5219_bright","XH 5219 明亮","作者3號：明亮方向的5219配色。","適合日常與人像；普通／標準畫面輸入。","portrait"},
{"xh_5219_hasselblad","XH 5219 哈蘇風格","作者7號：哈蘇風格方向的5219配色。","適合人像與生活記錄；可與明亮版對照。","portrait"},
};
#endif

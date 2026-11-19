from context import *
spec=[('film_cinestill','CineStill'),('fuji_400h','400H'),('fuji_astia100f','Astia'),('fuji_classicchrome','Classic'),('fuji_eterna500','Eterna'),
 ('fuji_velvia50','Velvia'),('kodak_2383','2383'),('kodak_ektar100','Ektar'),('mono_acros','Acros'),('pentax_satobi','Satobi'),('ricoh_retro','Retro'),
 ('xh_5219_bright','5219亮'),('xh_5219_hasselblad','5219哈蘇')]
anchor='static const char*detail_short_name(const char*id){'
patch(T/'base/nx-lut-lite/src/menu_lut_detail_v2.inc',anchor,anchor+'\n'+''.join(' if(!strcmp(id,'+json.dumps(id)+'))return '+json.dumps(label,ensure_ascii=False)+';\n'for id,label in spec))
print('PASS distinct compact tabs for new LUTs',flush=True)

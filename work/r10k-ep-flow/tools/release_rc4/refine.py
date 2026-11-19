from context import *
U=T/'base/nx-lut-lite/src'
patch(T/'base/nx-lut-lite/lut-core.sh','    reload_pending\n    # Promote an existing','    reload_pending\n    rm -f "$ST/detail-parameters" || die OLD_DETAIL_RECEIPT_INVALIDATION_FAILED\n    # Promote an existing')
patch(U/'menu.c','static void key(unsigned code,int value){\n if(code>=sizeof down','static void key(unsigned code,int value){\n /* The factory resolution command performs its own S1 GUI refresh. Keep\n  * our menu mapped while that bounded transaction is still running. */\n if(video_child>0&&video_command>=1&&video_command<=4&&(code==NXL_S1||code==126||code==83))return;\n if(code>=sizeof down')
print('PASS receipt invalidation and native refresh/menu boundary')

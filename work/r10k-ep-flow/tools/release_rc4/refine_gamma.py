from context import *
import shutil
p=T/'base/nx-lut-lite/gamma-reload.sh'
patch(p,'  sleep 0.1','  sleep 1')
patch(p,'gamma_confirm(){\n wanted=$1;phase=$2','gamma_queue_confirm(){\n wanted=$1;phase=$2')
patch(p,' # Keep the Gamma response as the final record,','}\ngamma_confirm(){\n gamma_queue_confirm "$1" "$2"\n # Keep the Gamma response as the final record,')
patch(p,'        NXL_LUT_DEFER_RELOAD=1 /bin/sh "$ENGINE" clean-log-release','        gamma_queue_confirm FACTORY FACTORY_QUEUED\n        NXL_LUT_DEFER_RELOAD=1 /bin/sh "$ENGINE" clean-log-release')
shutil.copyfile(p,M/p.name)
print('PASS bounded native queue confirmation precedes factory LUT cleanup')

/* Compile-only reachability fixture. Not included in the camera build. */
#include "../src/rtos_front.c"
__attribute__((used)) int p49_cc_reachability(void){
 return cc_dma_arm_config()+cc_dma_wait_config()+cc_commit()+cc_dma_cleanup()+p49_pd_record_factory();
}

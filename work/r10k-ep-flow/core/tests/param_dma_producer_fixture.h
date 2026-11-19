/* Exact production bodies. */
static volatile uint32_t *cc(void){return ptr(ctx.cap+P48_CC_OFFSET);}
static volatile uint32_t *cc_dma(void){return ptr(ctx.cap+P49_PD_OFFSET);}
static int cc_configured(void){return *ptr(P39_TASK)==0x80592640u&&(*ptr(P39_TASK+3364u)&255u)==2u&&*ptr(P39_TASK+3388u)==0x04000001u;}
static volatile uint32_t *gt(void){return ptr(ctx.cap+GT_OFFSET);}
static IQ_FN int p49_pd_record_single(void){
 volatile uint32_t*d=cc_dma();if(d[P49_PD_W_ACTIVE]||d[P49_PD_W_INFLIGHT]||cc()[71]||!cc_configured()||!cc()[3]||!gt()[GT_BEFORE_LOAD]||gt()[GT_CONFIG_LOADS]!=2||gt()[GT_CONFIG_RETURN]!=1||gt()[GT_REFRESH_RETURN]!=1||gt()[GT_FINAL_LOAD_STATUS])return 0;
 for(unsigned i=0;i<64;i++)d[i]=0;d[0]=P49_PD_MAGIC;d[1]=P49_PD_SCHEMA;d[2]=ctx.nonce;d[3]=8;d[48]=P49_PD_MODE_SINGLE_CONFIG_IQ;d[49]=1;d[51]=2;d[52]=gt()[3];d[53]=1;barrier();uint32_t copy[64];for(unsigned i=0;i<64;i++)copy[i]=d[i];return p49_pd_single_audit_valid(copy,ctx.nonce);
}

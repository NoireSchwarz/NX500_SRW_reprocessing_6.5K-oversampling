/* NX5001.12 CS1 SetConfig call-through. Keep the initial geometry SRAM load.
 * The original manager mode0 then recalculates all Still IQ and loads bank1
 * again (803c595c). Submit source IQ after that calculation, before its tail
 * load; never before the geometry load where factory IQ overwrites it.
 * Only pinned manager style0 is supported. No original text/vtable/handler
 * patch, extra reload, synthetic IRQ, new event route or blind DMA wait. */
#define IQ_FN __attribute__((noinline,section(".text.wb")))
static int iq_ram(uint32_t p,unsigned bytes){return p>=0x81000000u&&!(p&3u)&&(uint64_t)p+bytes<=0x94000000ULL;}
static IQ_FN int iq_final_source(uint32_t initial_status){
 uint32_t manager=(uint32_t)native(0x8009c51cu,0,0,0);
 if(manager!=0x81428374u||*ptr(manager)!=0x805751d0u||*(volatile unsigned char*)(manager+4u)!=0)return 0;
 /* Original8009c614(mode0)->802b0454: AE setup, mode0 configure, update,
  * metadata snapshot and AE flag. Split only the update's final tail load. */
 uint32_t ae=(uint32_t)native(0x802c8638u,0,0,0);native(0x802c9000u,ae,0,0);
 uint32_t engine=(uint32_t)native(0x8029b480u,0,0,0);
 if(engine!=0x81550224u)return 0;
 uint32_t dispatcher=*ptr(engine+0x70u);
 if(!iq_ram(dispatcher,0x1f0u)||*ptr(dispatcher)!=0x8062e398u)return 0;
 uint32_t group=*ptr(dispatcher+12u);
 if(!iq_ram(group,0x174u)||*ptr(group)!=0x8062f5f8u||*(volatile unsigned char*)(group+13u)!=1u)return 0;
 native(0x8038557cu,dispatcher,0,0);
 if(*ptr(dispatcher+12u)!=group||*ptr(group)!=0x8062f5f8u||*(volatile unsigned char*)(group+13u)!=1u||
  *(volatile unsigned char*)(dispatcher+4u)!=0||*(volatile unsigned char*)(group+12u)!=0||!cc_dma_configured_idle())return 0;
 /* Same mode-byte writes as original80385730(mode0), then the unmodified
  * collection calculator. 803c595c would immediately load after this call. */
 *(volatile unsigned char*)(dispatcher+4u)=0;*(volatile unsigned char*)(group+12u)=0;
 native(0x803c5668u,group,0,0);
 if(*ptr(engine+0x70u)!=dispatcher||*ptr(dispatcher+12u)!=group||*ptr(group)!=0x8062f5f8u||
  *(volatile unsigned char*)(group+13u)!=1||!cc_dma_configured_idle()||!gt_prepare())return 0;
 volatile uint32_t*q=gt(),*f=ptr(ctx.cap+P48_BATCH_FROZEN_OFFSET);
 q[GT_INITIAL_LOAD_STATUS]=initial_status;q[GT_CONFIG_LOADS]=1;q[GT_REFRESH_CALLED]=q[GT_REFRESH_RETURN]=1;
 q[GT_NATIVE_GROUP]=group;q[GT_NATIVE_GROUP_VTABLE]=*ptr(group);
 if(!cc_prepare_fixed(f+P48_F_CC1)||!cc_apply()||!gt_apply())return 0;
 q[GT_BEFORE_LOAD]=q[GT_FINAL_LOAD_CALLED]=1;q[GT_CONFIG_LOADS]++;barrier();
 q[GT_FINAL_LOAD_STATUS]=(uint32_t)native(0x8017da10u,0x1cu,1,0);
 if(q[GT_FINAL_LOAD_STATUS])return 0;
 native(0x803856acu,dispatcher,0,0);native(0x80385728u,dispatcher,0,0);native(0x8029bde8u,engine,0,0);
 return 1;
}
static IQ_FN int32_t config_source_iq(uint32_t job,uint32_t prior_state){
 uint32_t child=*ptr(P39_TASK+3428u),owner,setup;
 if(!idle()||*ptr(job)!=0x04000001u||!child||*ptr(child)!=0x805926c8u)return -1;
 for(unsigned i=0;i<6;i++)ptr(P39_TASK+3388u)[i]=*ptr(job+4*i);
 *(volatile unsigned char*)(P39_TASK+3364u)=2;*ptr(0x81540f38u)=0x04000001u;*ptr(P39_TASK+3436u)=child;*ptr(0x81089a94u)=1;barrier();
 native(0x801da664u,child,job,*ptr(0x81543d48u));
 native(0x801da548u,child,*ptr(0x81543d40u),0);
 owner=*ptr(0x81543d40u);setup=*ptr(0x81543d48u);
 if(!cc_dma_owner(owner)||!cc_configured())return -1;
 native(0x8021a964u,owner,setup,0);native(0x8021ade0u,job,owner,0);native(0x8021ac54u,owner,*ptr(job+16u),0);
 if(!cc_dma_configured_idle()||*ptr(0x81543d40u)!=owner||*ptr(0x81543d48u)!=setup||!batch_freeze(1,prior_state))return -1;
 uint32_t initial=(uint32_t)native(0x8021b314u,owner,0,0);
 if(initial){gt()[GT_ERROR]=2;gt()[GT_INITIAL_LOAD_STATUS]=initial;return -1;}
 native(0x8021b320u,owner,0,0);*ptr(0x81089a94u)=0xffffffffu;
 if(!iq_final_source(initial)){gt()[GT_ERROR]=1;return -1;}
 native(0x801da9ccu,child,0,0);
 gt()[GT_CONFIG_RETURN]=1;barrier();return 0;
}
static IQ_FN int p49_pd_record_single(void){
 volatile uint32_t*d=cc_dma();if(d[P49_PD_W_ACTIVE]||d[P49_PD_W_INFLIGHT]||cc()[71]||!cc_configured()||!cc()[3]||!gt()[GT_BEFORE_LOAD]||gt()[GT_CONFIG_LOADS]!=2||gt()[GT_CONFIG_RETURN]!=1||gt()[GT_REFRESH_RETURN]!=1||gt()[GT_FINAL_LOAD_STATUS])return 0;
 for(unsigned i=0;i<64;i++)d[i]=0;d[0]=P49_PD_MAGIC;d[1]=P49_PD_SCHEMA;d[2]=ctx.nonce;d[3]=8;d[48]=P49_PD_MODE_SINGLE_CONFIG_IQ;d[49]=1;d[51]=2;d[52]=gt()[3];d[53]=1;barrier();uint32_t copy[64];for(unsigned i=0;i<64;i++)copy[i]=d[i];return p49_pd_single_audit_valid(copy,ctx.nonce);
}

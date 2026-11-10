/* The original ref_sem SVC returns a 12-byte T_RSEM. OEM getvalue at
 * 0x804dcf7c reads word2 as the count. No get/tryget/put, no ISR calls.
 * Invalid IDs are recorded without entering the kernel. A failed query is
 * evidence unavailable; it is never converted to a free semaphore.
 */
static __attribute__((noinline)) void still_audit(unsigned stage){
 if(!stage||stage>SLA_RECORDS)return;
 uint32_t w[SLA_WORDS];w[0]=SLA_MAGIC;w[1]=1;w[2]=ctx.nonce;w[3]=stage;
 w[4]=*ptr(P39_TASK+3364u);w[5]=*ptr(0x81540f38u);
 const uint32_t holder[2]={SLA_SEM_A,SLA_SEM_B};
 for(unsigned i=0;i<2;i++){
  unsigned k=i?11:6;w[k]=*ptr(holder[i]);w[k+1]=(uint32_t)-22;
  uint32_t info[4]={SLA_UNKNOWN,SLA_UNKNOWN,SLA_UNKNOWN,0xa55a3cc3u};
  if(w[k]&&w[k]<=65535u)w[k+1]=(uint32_t)native(SLA_REF_SEM,w[k],(uint32_t)(uintptr_t)info,0);
  for(unsigned j=0;j<3;j++)w[k+2+j]=info[j];
  w[16+i]=*ptr(holder[i]);
  if(info[3]!=0xa55a3cc3u)w[k+1]=(uint32_t)-75;
 }
 w[18]=sla_checksum(w);w[19]=ctx.nonce^stage^w[18]^SLA_MAGIC;
 volatile uint32_t *out=ptr(ctx.cap+SLA_OFFSET+(stage-1)*SLA_WORDS*4u);
 out[0]=0;barrier();for(unsigned i=1;i<SLA_WORDS;i++)out[i]=w[i];barrier();out[0]=SLA_MAGIC;barrier();
}

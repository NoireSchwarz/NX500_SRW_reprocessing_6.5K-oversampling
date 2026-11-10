/* Configuration changes are deduplicated. Native capture still requires
 * a fresh photo inventory event; a retained idle shadow is not a match. */
static uint64_t ic_last_poll,ic_last_record;
static uint32_t ic_previous_hash;
static struct ic_sample ic_observation;
static int ic_linux_read(void *unused,uint32_t at,void *out,unsigned bytes){(void)unused;return mapped_read(at,out,bytes,0);}
static void isp_compare_sample(unsigned sequence){
 uint64_t begin=now();if(ic_last_poll&&begin-ic_last_poll<10000000u)return;ic_last_poll=begin;
 (void)ic_capture(ic_linux_read,NULL,&ic_observation,0,0);
 /* CS0 preview changes do not force a full record every preview frame. */
 uint32_t h=ic_hash(ic_observation.bank[1],sizeof ic_observation.bank[1])^ic_hash(ic_observation.roots_before,sizeof ic_observation.roots_before)^ic_hash(ic_observation.wb+6,24)^ic_observation.mask^ic_observation.stable;
 if(!ic_last_record||h!=ic_previous_hash||begin-ic_last_record>=1000000000ULL){
  if(frame(context_file,12,sequence,begin,now(),&ic_observation,sizeof ic_observation)){ic_last_record=begin;ic_previous_hash=h;}
 }
}

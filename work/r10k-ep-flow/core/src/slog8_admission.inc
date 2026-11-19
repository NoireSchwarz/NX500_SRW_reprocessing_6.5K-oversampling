/* Exact production ordinary-RAM neutral-slot admission. */
static int slog8_neutral_slot(void){
 unsigned char part[4096];uint32_t h=2166136261u;
 for(unsigned at=0;at<19712u;){unsigned n=19712u-at;if(n>sizeof part)n=sizeof part;
  if(!physical_read(NULL,0x81119f00u+at,part,n))return 0;
  for(unsigned i=0;i<n;i++)h=(h^part[i])*16777619u;at+=n;
 }return h==0xf082d265u;
}

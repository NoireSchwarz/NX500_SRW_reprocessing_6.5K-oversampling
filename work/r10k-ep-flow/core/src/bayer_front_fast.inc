/* SPDX-License-Identifier: AGPL-3.0-only. Stream decode, no row audit files.
 * The asset SHA is verified once in p48_preflight_assets before native work.
 * Decoder bounds/statistics remain checked; per-row/full-frame hashes omitted.
 */
static int p32_front_open(struct p32_front*f,const char*input,const char*refs,const char*dir,FILE*log){
 (void)refs;(void)dir;memset(f,0,sizeof *f);f->log=log;
 if(p48_selected_ready){f->source=selected_file;selected_file=NULL;if(!selected_unchanged(f->source)){f->error="SELECTED_SRW_CHANGED";return 0;}}
 else f->source=fopen(input,"rb");if(!f->source){f->error="SRW_OPEN_FAILED";return 0;}
 const char*why="";if(!srw_parse(f->source,&f->info,&why)){f->error=why;return 0;}
 if(f->info.width!=6496||f->info.height!=4336||f->info.bits!=14||f->info.compression!=32773||f->info.offset!=p48_source->strip_offset||f->info.bytes!=p48_source->strip_bytes){f->error="FIXED_SRW_GEOMETRY_CHANGED";return 0;}
 memset(f->left,0xa5,16);memset(f->middle,0x5a,16);memset(f->right,0xa5,16);f->error="";return 1;
}
static int p32_front_row(struct p32_front*f,const unsigned char*raw){
 if(stop_requested||f->stored>=P31_STAGE_HEIGHT){f->error="DECODE_CANCELLED_OR_ROW_OVERFLOW";return 0;}p31_layout_row(f->layout,raw);
 for(unsigned i=0;i<16;i++)if(f->left[i]!=0xa5||f->middle[i]!=0x5a||f->right[i]!=0xa5){f->error="CPU_ROW_CANARY_CHANGED";return 0;}
 if(!f->write_row(f->sink_user,f->stored,f->layout,sizeof f->layout)){f->error="CAP_ROW_WRITE_FAILED";return 0;}f->stored++;return 1;
}
static int p32_front_decoded_row(void*user,unsigned row,const uint16_t*pixels,unsigned count){
 struct p32_front*f=user;if(count!=P31_RAW_WIDTH||row!=f->matched){f->error="SOURCE_ROW_ORDER_OR_WIDTH";return 0;}
 /* srw_decode clamps14-bit values and collects the same range statistics.
  * The owned ring is already little endian on the target, so serialize once. */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
 if(!p32_front_row(f,(const unsigned char*)pixels))return 0;
#else
 for(unsigned x=0;x<count;x++){f->raw[2*x]=(unsigned char)pixels[x];f->raw[2*x+1]=(unsigned char)(pixels[x]>>8);}
 if(!p32_front_row(f,f->raw))return 0;
#endif
 f->matched++;return 1;
}
static int p32_front_write(struct p32_front*f,void*user,int(*emit)(void*,uint32_t,const unsigned char*,size_t)){
 f->sink_user=user;f->write_row=emit;for(unsigned y=0;y<P31_OFFSET_Y;y++)if(!p32_front_row(f,NULL))return 0;
 const char*why="";if(!srw_decode(f->source,&f->info,&f->stats,p32_front_decoded_row,f,&stop_requested,&why)){if(!*f->error)f->error=why;return 0;}
 if(f->matched!=4336||f->stats.rows!=4336||f->stats.pixels!=6496u*4336u||f->stats.max_value>16383||
   (p48_selected_ready?!selected_unchanged(f->source):(f->stats.min_value!=p48_source->expected_min||f->stats.max_value!=p48_source->expected_max||f->stats.zeros!=p48_source->expected_zeros))){f->error="DECODED_GEOMETRY_OR_STATS_MISMATCH";return 0;}
 for(unsigned y=f->stored;y<P31_STAGE_HEIGHT;y++)if(!p32_front_row(f,NULL))return 0;
 f->verified=f->stored==4384;return f->verified;
}
static int p32_front_close(struct p32_front*f){int ok=!f->source||!fclose(f->source);f->source=NULL;return ok;}

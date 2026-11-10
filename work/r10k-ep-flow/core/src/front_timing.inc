/* No sleeps, only monotonic stage boundaries in RAM until terminal save. */
struct front_stamp {const char*name;uint64_t ns;};
static struct front_stamp front_stamps[32];static unsigned front_stamp_count;
static void front_mark(const char*name){if(front_stamp_count<32){front_stamps[front_stamp_count].name=name;front_stamps[front_stamp_count++].ns=ns();}}
static int front_timing_save(const char*dir){
 char path[1200];snprintf(path,sizeof path,"%s/front-timing.json",dir);FILE*f=fopen(path,"w");if(!f)return 0;
 fprintf(f,"{\"schema\":1,\"version\":\"R8l\",\"source_hash_ms\":%llu,\"settings_parse_ms\":%llu,\"native_pair_verify_ms\":%llu,\"stages\":[",(unsigned long long)(selected_hash_ns/1000000),(unsigned long long)(selected_settings_ns/1000000),(unsigned long long)(selected_pair_ns/1000000));
 for(unsigned i=0;i<front_stamp_count;i++)fprintf(f,"%s{\"name\":\"%s\",\"mono_ns\":%llu,\"since_previous_ms\":%llu}",i?",":"",front_stamps[i].name,(unsigned long long)front_stamps[i].ns,(unsigned long long)(i?(front_stamps[i].ns-front_stamps[i-1].ns)/1000000:0));
 fputs("]}\n",f);int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok;
}

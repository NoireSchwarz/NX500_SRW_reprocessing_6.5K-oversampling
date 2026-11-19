/* Execute the production maps scanner, not a reimplementation of it. */
#define main fixture_production_main
#include "../src/nxl-video-probe.c"
#undef main
int main(int argc,char**argv){
 _Static_assert(sizeof(struct vz_runtime)+sizeof(struct vz_state)<=8192u,"Bound Linux observer context, not RTOS allocation");
 struct runtime r={.dev=-1,.mem=-1,.pid=247,.start=5678};
 struct vz_runtime x={.r=&r,.syscall_fd=-1,.pthread_layout_verified=1,.scan={.fd=-1}};
 struct vz_maps m={0};unsigned step,max_lines=0;int result=0;
 for(step=0;step<512u;step++){
  unsigned previous=m.lines;result=vz_runtime_maps_step(&x,step==0?1:0,&m);
  if(m.lines-previous>max_lines)max_lines=m.lines-previous;
  if(argc>1&&!strcmp(argv[1],"cancel")&&step==3u){result=vz_runtime_maps_step(&x,-1,&m);break;}
  if(result)break;struct timespec delay={0,100000000};clock_nanosleep(CLOCK_MONOTONIC,0,&delay,NULL);
 }
 printf("{\"result\":%d,\"status\":%u,\"steps\":%u,\"lines\":%u,\"bytes\":%u,\"max_lines_per_step\":%u,\"failure\":%u,\"errno\":%u,\"CAP_maps\":%u,\"pthread_mappings\":%u,\"pthread_base\":%u,\"closed\":%u}\n",result,m.status,step+1u,m.lines,m.bytes,max_lines,m.failure_stage,m.read_errno,m.count,m.pthread_seen,m.pthread_base,x.scan.fd<0);
 vz_runtime_close(&x);return result?0:2;
}

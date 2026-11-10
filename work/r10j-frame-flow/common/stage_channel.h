/* Passive local status, never an execution or resource authority.
 * Abstract datagrams have no SD/directory entry, no acknowledgement, no
 * SIGPIPE, and no wait for a display client. Missing/full UI is harmless. */
#ifndef NXL_STAGE_CHANNEL_H
#define NXL_STAGE_CHANNEL_H
#include <sys/socket.h>
#include <sys/un.h>
static socklen_t np_stage_address(struct sockaddr_un*a,const char*operation){
 static const char prefix[]="nxl51.stage.";
 if(!np_operation(operation))return 0;
 memset(a,0,sizeof *a);a->sun_family=AF_UNIX;
 memcpy(a->sun_path+1,prefix,sizeof prefix-1);memcpy(a->sun_path+sizeof prefix,operation,32);
 return (socklen_t)(offsetof(struct sockaddr_un,sun_path)+sizeof prefix+32);
}
static int np_stage_open(const char*operation){
 struct sockaddr_un address;socklen_t bytes=np_stage_address(&address,operation);if(!bytes){errno=EINVAL;return -1;}
 int fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return -1;
 if(bind(fd,(const struct sockaddr*)&address,bytes)){int error=errno;close(fd);errno=error;return -1;}return fd;
}
static void np_stage_send(const struct np_record*r){
 if(r->version!=3)return;
 int saved=errno;struct sockaddr_un address;socklen_t bytes=np_stage_address(&address,r->operation);
 if(bytes){int fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd>=0){
  (void)sendto(fd,r,sizeof *r,MSG_DONTWAIT|MSG_NOSIGNAL,(const struct sockaddr*)&address,bytes);close(fd);
 }}errno=saved;
}
#endif

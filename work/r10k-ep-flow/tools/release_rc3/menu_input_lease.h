/* SPDX-License-Identifier: AGPL-3.0-only
 * Temporary evdev ownership for a modal. No event injection or native key mask.
 * Linux evdev also releases the grab when the owning open file is closed.
 */
#ifndef NXL_MENU_INPUT_LEASE_H
#define NXL_MENU_INPUT_LEASE_H
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
struct nxl_input_lease {int fd[2];unsigned mask;};
static int nxl_input_grab(int fd,int take){
 int rc;do{rc=ioctl(fd,_IOW('E',0x90,int),take);}while(rc<0&&errno==EINTR);return rc==0;
}
static int nxl_input_lease_release(struct nxl_input_lease*l){
 int ok=1;
 for(unsigned i=0;i<2;i++)if(l->mask&(1u<<i)){
  if(!nxl_input_grab(l->fd[i],0)){close(l->fd[i]);ok=0;}
  l->fd[i]=-1;l->mask&=~(1u<<i);
 }
 return ok;
}
static int nxl_input_lease_take(struct nxl_input_lease*l,const int fd[2]){
 if(l->mask)return l->mask==3&&l->fd[0]==fd[0]&&l->fd[1]==fd[1];
 if(fd[0]<0||fd[1]<0||fd[0]==fd[1])return 0;
 for(unsigned i=0;i<2;i++){
  if(!nxl_input_grab(fd[i],1)){(void)nxl_input_lease_release(l);return 0;}
  l->fd[i]=fd[i];l->mask|=1u<<i;
 }
 return 1;
}
#endif

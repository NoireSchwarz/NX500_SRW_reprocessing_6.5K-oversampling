/* SPDX-License-Identifier: AGPL-3.0-only
 * Only our own windows are configured. Let the camera WM composite them
 * instead of bypassing it with override_redirect after USB transitions.
 */
#ifndef NXL_MENU_WINDOW_POLICY_H
#define NXL_MENU_WINDOW_POLICY_H
static Window nxl_window_parent(Display*d,Window own){
 Atom type=0;int format=0;unsigned long n=0,left=0;unsigned char*p=NULL;Window parent=0;
 if(XGetWindowProperty(d,RootWindow(d,DefaultScreen(d)),XInternAtom(d,"_NET_ACTIVE_WINDOW",False),0,1,False,33,&type,&format,&n,&left,&p)==Success&&type==33&&format==32&&n==1&&!left&&p)parent=*(unsigned long*)p;
 if(p)XFree(p);return parent>PointerRoot&&parent!=own?parent:None;
}
/* Fixed root coordinates for our palette. Keep compositor management. */
static void nxl_window_geometry(Display*d,Window own,int x,int y,int width,int height){
 long geometry[18]={1|2|4|8|16|32|256|512,x,y,width,height,width,height,width,height,0,0,0,0,0,0,width,height,StaticGravity};
 XChangeProperty(d,own,XInternAtom(d,"WM_NORMAL_HINTS",False),XInternAtom(d,"WM_SIZE_HINTS",False),32,PropModeReplace,(unsigned char*)geometry,18);
 XMoveResizeWindow(d,own,x,y,(unsigned)width,(unsigned)height);
 XEvent request;memset(&request,0,sizeof request);request.xclient.type=ClientMessage;request.xclient.display=d;request.xclient.window=own;
 request.xclient.message_type=XInternAtom(d,"_NET_MOVERESIZE_WINDOW",False);request.xclient.format=32;
 request.xclient.data.l[0]=StaticGravity|(15L<<8)|(1L<<12);request.xclient.data.l[1]=x;request.xclient.data.l[2]=y;request.xclient.data.l[3]=width;request.xclient.data.l[4]=height;
 XSendEvent(d,RootWindow(d,DefaultScreen(d)),False,SubstructureRedirectMask|SubstructureNotifyMask,&request);
}
struct nxl_window_target{Window own;int x,y,width,height;unsigned checks;unsigned long due;};
static struct nxl_window_target nxl_window_targets[2];
static void nxl_window_place(Display*d,Window own,int x,int y,int width,int height,unsigned long now){
 unsigned i=0;if(nxl_window_targets[0].own&&nxl_window_targets[0].own!=own)i=1;
 nxl_window_targets[i]=(struct nxl_window_target){own,x,y,width,height,0,now+250UL};
 nxl_window_geometry(d,own,x,y,width,height);
}
static void nxl_window_forget(Window own){for(unsigned i=0;i<2;i++)if(nxl_window_targets[i].own==own)nxl_window_targets[i].own=0;}
static void nxl_window_tick(Display*d,unsigned long now){
 for(unsigned i=0;i<2;i++){
  struct nxl_window_target*t=nxl_window_targets+i;if(!t->own||t->checks>=4||(long)(now-t->due)<0)continue;
  XWindowAttributes a;int x=0,y=0;Window child=None;
  if(!XGetWindowAttributes(d,t->own,&a)||!XTranslateCoordinates(d,t->own,RootWindow(d,DefaultScreen(d)),0,0,&x,&y,&child)){t->checks=4;continue;}
  t->checks++;int ok=x==t->x&&y==t->y&&a.width==t->width&&a.height==t->height;
  fprintf(stderr,"MENU_GEOMETRY window=%lx actual=%d,%d,%d,%d expected=%d,%d,%d,%d match=%d check=%u managed=1\n",t->own,x,y,a.width,a.height,t->x,t->y,t->width,t->height,ok,t->checks);
  if(!ok&&t->checks<4)nxl_window_geometry(d,t->own,t->x,t->y,t->width,t->height);
  t->due=now+500UL;
 }
}
static void nxl_window_prepare(Display*d,Window own,Window parent,int interactive){
 if(!parent)parent=nxl_window_parent(d,own);
 Atom cardinal=6,atom=4;unsigned long opaque=0xffffffffUL,bypass=2,hints[9]={1,(unsigned long)!!interactive,0,0,0,0,0,0,0},motif[5]={2,0,0,0,0};
 Atom type=XInternAtom(d,interactive?"_NET_WM_WINDOW_TYPE_UTILITY":"_NET_WM_WINDOW_TYPE_NOTIFICATION",False);
 Atom states[3]={XInternAtom(d,"_NET_WM_STATE_ABOVE",False),XInternAtom(d,"_NET_WM_STATE_SKIP_TASKBAR",False),XInternAtom(d,"_NET_WM_STATE_SKIP_PAGER",False)};
 XChangeProperty(d,own,XInternAtom(d,"_NET_WM_WINDOW_TYPE",False),atom,32,PropModeReplace,(unsigned char*)&type,1);
 XChangeProperty(d,own,XInternAtom(d,"_NET_WM_STATE",False),atom,32,PropModeReplace,(unsigned char*)states,3);
 XChangeProperty(d,own,XInternAtom(d,"_NET_WM_BYPASS_COMPOSITOR",False),cardinal,32,PropModeReplace,(unsigned char*)&bypass,1);
 XChangeProperty(d,own,XInternAtom(d,"_NET_WM_WINDOW_OPACITY",False),cardinal,32,PropModeReplace,(unsigned char*)&opaque,1);
 Atom wm=XInternAtom(d,"WM_HINTS",False);XChangeProperty(d,own,wm,wm,32,PropModeReplace,(unsigned char*)hints,9);
 XChangeProperty(d,own,XInternAtom(d,"_MOTIF_WM_HINTS",False),XInternAtom(d,"_MOTIF_WM_HINTS",False),32,PropModeReplace,(unsigned char*)motif,5);
 if(parent)XChangeProperty(d,own,XInternAtom(d,"WM_TRANSIENT_FOR",False),33,32,PropModeReplace,(unsigned char*)&parent,1);
 XWindowAttributes a;if(XGetWindowAttributes(d,own,&a))nxl_window_geometry(d,own,a.x,a.y,a.width,a.height);
}
#endif

/* SPDX-License-Identifier: AGPL-3.0-only */
#include "wifi_rvf_ui.h"

static int rvf_related_window(Window window, Window expected, pid_t camera){
    Display *d=wifi_display;
    Atom pid_atom=XInternAtom(d,"_NET_WM_PID",True);
    const Atom cardinal=6; /* X11's predefined CARDINAL atom. */
    for(int i=0;i<8&&window>PointerRoot&&window!=DefaultRootWindow(d);i++){
        if(window==expected)return 1;
        Atom type=None;int format=0;unsigned long count=0,left=0;unsigned char *data=NULL;
        int ok=pid_atom!=None&&XGetWindowProperty(d,window,pid_atom,0,1,False,cardinal,
            &type,&format,&count,&left,&data)==Success&&type==cardinal&&format==32&&count==1&&data&&
            *(unsigned long*)data==(unsigned long)camera;
        if(data)XFree(data);
        if(ok)return 1;
        Window root,parent,*children=NULL;unsigned n=0;
        int tree=XQueryTree(d,window,&root,&parent,&children,&n);
        if(children)XFree(children);
        if(!tree||parent==window)break;
        window=parent;
    }
    return 0;
}

static int rvf_focus(const struct nxl_rvf_ui_request *r){
    Window focus;int revert;
    XGetInputFocus(wifi_display,&focus,&revert);
    return rvf_related_window(focus,r->window,r->camera);
}

static unsigned rvf_mobile_key(const struct nxl_rvf_mobile_menu *menu){
    if(!menu->valid||menu->view||menu->busy_and_disabled[0]||menu->busy_and_disabled[2]||
       menu->row<0||menu->row>3)return 0;
    /* Original MOBILE navigation and OK, from the NX500 factory key table. */
    return menu->row==1?104u:menu->row==2?80u:88u;
}

static int rvf_gui_click(unsigned key){
    char keys[32];
    if(!XQueryKeymap(wifi_display,keys)||((unsigned char)keys[key/8]&(1u<<(key%8))))return 0;
    wifi_xerror=0;
    int down=XTestFakeKeyEvent(wifi_display,key,True,0);
    int up=XTestFakeKeyEvent(wifi_display,key,False,0);
    XSync(wifi_display,False);
    return down&&up&&!wifi_xerror;
}

static int rvf_ui_tick(unsigned long now){
    static unsigned long next;
    static struct nxl_rvf_ui_request last;
    static int sent_wifi,sent_select,sent_exit,nav_count;
    static unsigned long mobile_since,nav_at;
    if(!wifi_display||!wifi_xtest||(long)(now-next)<0)return 1;
    next=now+180;
    struct nxl_rvf_ui_request r;
    if(!nxl_rvf_ui_read(&r)||now-(unsigned long)r.issued>42000UL)return 1;
    if(r.owner!=last.owner||r.owner_tick!=last.owner_tick||r.issued!=last.issued||r.command!=last.command){
        last=r;sent_wifi=sent_select=sent_exit=nav_count=0;mobile_since=nav_at=0;
    }
    if((r.command==NXL_RVF_UI_EXIT&&sent_exit==3)||(r.command==NXL_RVF_UI_OPEN&&sent_select)||
       wifi_xheld||now-(unsigned long)r.issued<160)return 1;
    uint32_t gui,mode;struct nxl_rvf_mobile_menu menu;
    if(!nxl_rvf_ui_snapshot(r.camera,r.camera_tick,&gui,&mode,r.command==NXL_RVF_UI_OPEN?&menu:NULL)||!rvf_focus(&r))return 1;
    if(r.command==NXL_RVF_UI_EXIT){
        int screen=gui==NXL_RVF_GUI_VIEWFINDER?1:gui==NXL_RVF_GUI_MOBILE?2:0;
        if(!screen||(sent_exit&screen))return 1;
        /* BYEBYE can close the network stream before the original RVF GUI
         * returns. Send its ordinary MENU once per owned native screen. */
        if(rvf_gui_click(135)){
            sent_exit|=screen;
            event_log("WIFI_NATIVE_RVF_MENU",screen,(int)mode);
        }
        return 1;
    }
    if(mode!=0||!gui)return 1;
    if(!sent_wifi&&gui!=NXL_RVF_GUI_MOBILE&&gui!=NXL_RVF_GUI_VIEWFINDER){
        sent_wifi=1;
        return wifi_native_click();
    }
    if(gui==NXL_RVF_GUI_MOBILE){
        if(!mobile_since){mobile_since=now;return 1;}
        if(now-mobile_since<250||now-nav_at<300||nav_count>=5)return 1;
        unsigned key=rvf_mobile_key(&menu);
        if(!key)return 1;
        nav_at=now;
        if(rvf_gui_click(key)){
            if(key==104)sent_select=1;
            else nav_count++;
            event_log("WIFI_NATIVE_RVF_KEY",(int)key,menu.row);
        }
    }
    return 1;
}

/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_KEYS_H
#define NXL_KEYS_H
/* Linux evdev codes documented by ottokiksmaler/nx500_nx1_modding. */
#define NXL_EV 173
#define NXL_AEL 156
#define NXL_WIFI 215 /* NXKEY_MOBILE in the community's actual keyscan.c. */
#define NXL_S1 125
struct nxl_chord { int ev, ael, latched; };
static inline int nxl_combo(struct nxl_chord *s, unsigned code, int value) {
    if (value < 0 || value > 2) return 0;
    if (code == NXL_EV) s->ev = value != 0;
    if (code == NXL_AEL) s->ael = value != 0;
    if (!s->ev || !s->ael) s->latched = 0;
    if (value == 1 && s->ev && s->ael && !s->latched) {
        s->latched = 1;
        return 1;
    }
    return 0;
}
/* Deliver native key-up events before allowing the menu to take focus. */
/* SPDX-License-Identifier: AGPL-3.0-only */
enum { NXL_OPEN_MENU=1, NXL_TOGGLE_VLOG=2, NXL_OPEN_WIFI=4 };
struct nxl_shortcut { struct nxl_chord chord; int pending,wifi,blocked,wifi_armed,wifi_cancel,wifi_owned; };
static inline int nxl_activate(struct nxl_shortcut*s,unsigned code,int value){
 if(value!=0&&value!=1)return 0;
 if(code!=NXL_EV&&code!=NXL_AEL&&code!=NXL_WIFI){
  if(value==1&&(s->wifi||s->pending)){s->wifi_cancel=1;s->blocked=1;}
  return 0;
 }
 if(code==NXL_EV)s->chord.ev=value;
 else if(code==NXL_AEL)s->chord.ael=value;
 else {if(value&&!s->wifi){s->wifi_armed=1;s->wifi_cancel=0;}s->wifi=value;}
 if(s->chord.ael&&s->wifi)s->blocked=1;
 if(s->wifi&&(s->chord.ev||s->chord.ael))s->wifi_cancel=1;
 int candidate=s->chord.ev?(s->wifi?NXL_TOGGLE_VLOG:s->chord.ael?NXL_OPEN_MENU:0):0;
 if(candidate&&s->pending&&candidate!=s->pending)s->blocked=1;
 if(candidate&&!s->blocked)s->pending=candidate;
 if(!s->chord.ev&&!s->chord.ael&&!s->wifi){
  int result=s->blocked?0:s->pending?s->pending:s->wifi_armed&&!s->wifi_cancel&&!s->wifi_owned?NXL_OPEN_WIFI:0;
  s->pending=s->blocked=s->wifi_armed=s->wifi_cancel=s->wifi_owned=0;return result;
 }
 return 0;
}

/* Upper dial selects categories; rear dial switches the left panel.
 * Only D-pad UP/DOWN select items. AEL alone remains unused. */
static inline int nxl_sidebar_step(unsigned code) {
    return code==165?-1:code==163?1:0;
}
static inline int nxl_category_step(unsigned code) {
    switch (code) {
    case 178: return -1; /* LEFT / JOG1_CCW */
    case 177: return 1;  /* RIGHT / JOG1_CW */
    default: return 0;
    }
}
static inline int nxl_item_step(unsigned code) {
    switch (code) {
    case 72: case 165: return -1; /* UP */
    case 80: case 163: return 1;  /* DOWN */
    default: return 0;
    }
}
#endif

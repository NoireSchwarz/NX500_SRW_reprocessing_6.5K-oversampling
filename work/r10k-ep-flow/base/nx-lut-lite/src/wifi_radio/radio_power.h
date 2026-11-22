/* SPDX-License-Identifier: AGPL-3.0-only
 * Only the stock wlan.sh 0 -> 1 SDIO-enable operation. Never write 0 or reset.
 * As stock stop specifies, keep the shared chip powered for Bluetooth.
 */
#define CARD_PATH "/sys/devices/platform/dw_mmc_sdio.0/support_card"
static int card_state(void){
 char a[16]={0},b[16]={0},line[160],ha[31],hb[31];int x=read_word_text(CARD_PATH,a,sizeof a),y=read_word_text(CARD_PATH,b,sizeof b);
 size_t an=strnlen(a,15),bn=strnlen(b,15);for(size_t i=0;i<an;i++)snprintf(ha+2*i,3,"%02x",(unsigned char)a[i]);ha[2*an]=0;for(size_t i=0;i<bn;i++)snprintf(hb+2*i,3,"%02x",(unsigned char)b[i]);hb[2*bn]=0;
 snprintf(line,sizeof line,"RADIO_CARD_RAW read_a=%d read_b=%d a_hex=%s b_hex=%s",x,y,ha,hb);note(line);
 if(!x||!y){failed("RADIO_POWER_READ_FAILED");return-1;}if(strcmp(a,b)){failed("RADIO_POWER_CHANGED_DURING_READ");return-1;}if(!strcmp(a,"0"))return 0;if(!strcmp(a,"1"))return 1;failed("RADIO_POWER_VALUE_UNKNOWN");return-1;
}
static int radio_power_on(void){
 if(!trial.module_dirty||trial.phase!=P_POWER)return failed("RADIO_POWER_LEASE_REQUIRED");
 if(physical_usb()!=0||trial_stop||!loaders_idle())return failed("RADIO_POWER_CONDITION_CHANGED");
 for(unsigned i=0;i<4;i++){const struct actor*a=trial.actors+i;if(a->pid&&(!a->frozen||!actor_match(a)||actor_state(a->pid)!='T'))return failed("RADIO_POWER_ACTORS_NOT_FROZEN");}
 int power=card_state();if(power<0)return 0;if(power!=trial.old_power)return failed("RADIO_POWER_CHANGED_BEFORE_PREPARE");
 if(power==1){trial.power_confirmed=1;note("RADIO_POWER_ALREADY_ON no_write=1");if(!state_save())return failed("RADIO_POWER_CONFIRM_NOT_SAVED");return 1;}
 trial.power_on_issued=1;if(!state_save())return failed("RADIO_POWER_NOT_JOURNALED");
 int f=open(CARD_PATH,O_WRONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(f<0)return failed("RADIO_POWER_OPEN_FAILED");struct stat st;int valid=!fstat(f,&st)&&S_ISREG(st.st_mode);ssize_t n=-1;
 if(valid)n=write(f,"1\n",2);int closed=!close(f);
 char line[120];snprintf(line,sizeof line,"RADIO_POWER_ENABLE bytes=%ld close_ok=%d initial=0 requested=1",(long)n,closed);note(line);
 if(!valid||n!=2||!closed)return failed("RADIO_POWER_ENABLE_UNCONFIRMED");
 unsigned long long end=monotime()+2000;for(;;){int current=card_state();if(current==1)break;if(current<0||trial_stop||physical_usb()!=0||monotime()>=end)return failed("RADIO_POWER_ENABLE_NOT_READY");poll(NULL,0,50);}
 poll(NULL,0,100);if(card_state()!=1)return failed("RADIO_POWER_CHANGED_AFTER_ENABLE");trial.power_confirmed=1;if(!state_save())return failed("RADIO_POWER_CONFIRM_NOT_SAVED");note("RADIO_POWER_CONFIRMED stock_stop_keeps_power_on=1");return 1;
}

/* SPDX-License-Identifier: AGPL-3.0-only.
 * Migration for the observed R7d read-only initialization failure only.
 * These hashes bind the complete recorded result, both readiness snapshots,
 * empty trace/command streams, unspent-grant audit and exact event sequence.
 * This is not a generic way to clear an unknown native failure. */
struct preflight_pin { const char *leaf,*sha; unsigned bytes; };
static const struct preflight_pin preflight_pins[]={
 {"events.log","afb3aae4225d325cc6f803e49a47319fb1f4258296e807c838803f9be96df313",605},
 {"front_1/front/result.json","2672536d94518765af27ae0dac8f9039e8d587b411034d5fe2044a0fe76716d2",468},
 {"front_1/front/case_00/result.json","747fe29556b2818466716a2744cfc0077b1b130e689796440f759d28f605f988",6303},
 {"front_1/front/case_00/front_readiness_first.json","aa5a0757934990f221f965087790ae8ca1f8e22dab912d2f0056a0705fae4a78",3943},
 {"front_1/front/case_00/front_readiness_final.json","161849e4c5af85592a7304f7e5af4044c4fd10cc29570607451cbc08bfae7161",3947},
 {"front_1/front/p39_attempt_guard.txt","38b5a016cd44f74e69d2b0905ff5f574fb269054610f25a6aee5d3cb2045ab1a",168},
 {"front_1/front/trace_events.txt","e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",0},
 {"front_1/front/p39_commands.log","e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",0}
};
static int preflight_pin_file(const char *job,const struct preflight_pin *pin){
 char path[2048],data[8192],hash[65];
 if(!bounded(path,sizeof path,job,pin->leaf)||!no_symlink_parents(path))return 0;
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;
 struct stat st;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size==pin->bytes&&pin->bytes<sizeof data;
 size_t at=0;while(ok&&at<pin->bytes){ssize_t n=read(fd,data+at,pin->bytes-at);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}at+=(size_t)n;}
 if(close(fd))ok=0;
 if(!ok)return 0;digest(data,at,hash);return !strcmp(hash,pin->sha);
}
static int preflight_legacy_state(const struct session *s){
 return s->seq==5&&s->front_attempt==1&&!s->backend_attempt&&s->requires_reset&&!s->verified&&
  !strcmp(s->state,"RESTART_REQUIRED")&&!strcmp(s->front,"-")&&!strcmp(s->stage,"front")&&
  !strcmp(s->resume,"front")&&!strcmp(s->reason,"FRONT_FAILED_NATIVE_STATE_UNVERIFIED")&&
  !strcmp(s->outcome,"UNKNOWN_REQUIRE_RESTART")&&!strcmp(s->look,"raw422")&&s->strength==100;
}
static int preflight_legacy(const struct session *s){
 if(!preflight_legacy_state(s))return 0;
 for(unsigned i=0;i<sizeof preflight_pins/sizeof *preflight_pins;i++)
  if(!preflight_pin_file(s->job,preflight_pins+i))return 0;
 return 1;
}

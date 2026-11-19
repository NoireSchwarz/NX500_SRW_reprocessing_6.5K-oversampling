from context import *
p=T/'core/src/nxl-lutload.c'
patch(p,'int fd=-1,mem=-1,lock=-1,rc=1,started=0,wait_only,current=0;','int fd=-1,mem=-1,lock=-1,run=-1,rc=1,started=0,wait_only,current=0;')
patch(p,'    signal(SIGTERM,stop);','''    /* Take the supervisor's existing SRW lease only in the new hardware
     * loader. The old LUT guard must still permit SRW's own off handoff. */
    run=open("/tmp/nx-lut-lite/run.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if(run<0||flock(run,LOCK_EX|LOCK_NB)<0){rc=4;goto out;}
    signal(SIGTERM,stop);''')
patch(p,'if(fd>=0)close(fd);if(lock>=0)close(lock);','if(fd>=0)close(fd);if(run>=0)close(run);if(lock>=0)close(lock);')
host('nxl-lutload-final',p,M/'bin/nxl-lutload.arm')
print('PASS hardware loader also excludes active SRW supervisor')

#!/usr/bin/env python3
"""Execute the actual helper stack-restoration fragment under hostile timing.

Only process-memory I/O is modeled. The detached shell changes its old stack
before terminal cleanup, making a repeated write observable and incorrect.
"""
from pathlib import Path
import hashlib, json, subprocess, tempfile

R = Path(__file__).resolve().parents[1]
SOURCE = R / 'src/nxl-oem-restore.c'


def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 0
    for i in range(opening, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if not depth:
                return text[start:i + 1]
    raise AssertionError('Unclosed production function')


def main():
    raw = SOURCE.read_text()
    fragment = function(raw, 'static int restore_stack(void)')
    model = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
static unsigned stack_saved,stack_written,stack_restored,api_inflight;
static int attached,trace_stopped,procfd=42;
static uint32_t saved_stack[2],stack_address=0x8000,actual[2];
static unsigned writes,read_fails;
static ssize_t model_pwrite(int fd,const void *p,size_t n,off_t at){
 assert(fd==42&&n==8&&at==0x8000);
 assert(attached&&trace_stopped&&!api_inflight);
 memcpy(actual,p,8);writes++;return 8;
}
static int read_process(uint32_t at,void *p,size_t n){
 assert(at==0x8000&&n==8);if(read_fails)return 0;
 memcpy(p,actual,8);return 1;
}
#define pwrite model_pwrite
'''+fragment+r'''
static void setup(void){
 stack_saved=stack_written=1;stack_restored=api_inflight=0;
 attached=trace_stopped=1;writes=read_fails=0;
 saved_stack[0]=0x11223344;saved_stack[1]=0x55667788;
 actual[0]=actual[1]=0;
}
int main(void){
 setup();assert(restore_stack());assert(writes==1&&stack_restored);
 assert(!memcmp(actual,saved_stack,8));
 attached=trace_stopped=0;actual[0]=0xdeadbeef;actual[1]=0xcafef00d;
 assert(restore_stack());assert(writes==1);
 assert(actual[0]==0xdeadbeef&&actual[1]==0xcafef00d);

 setup();api_inflight=1;assert(!restore_stack());assert(!writes&&!stack_restored);
 setup();attached=trace_stopped=0;assert(!restore_stack());assert(!writes);
 setup();trace_stopped=0;assert(!restore_stack());assert(!writes);
 setup();read_fails=1;assert(!restore_stack());assert(writes==1&&!stack_restored);
 setup();stack_saved=stack_written=0;attached=trace_stopped=0;
 assert(restore_stack());assert(!writes);
 puts("PASS 6 actual-fragment stack lifecycle scenarios; no camera");
 return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='oem-cleanup-review-') as td:
        c = Path(td) / 'model.c'
        exe = Path(td) / 'model'
        c.write_text(model)
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(c), '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
    report = dict(format='NX51_INDEPENDENT_STACK_LIFECYCLE1', passed=True,
                  tests_run=6, hardware_tested=False, actual_fragment=True,
                  helper_source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                  tested_fragment_sha256=hashlib.sha256(fragment.encode()).hexdigest(),
                  script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  stdout=result.stdout,
                  limits=['Does not execute ptrace or on-camera native code.'])
    (R / 'results/independent-oem-cleanup.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()

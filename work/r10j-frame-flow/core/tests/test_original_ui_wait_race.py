#!/usr/bin/env python3
"""Execute OEM ARM Wait/Complete; model only logging and POSIX semaphores.

This checks the old synchronization hazard independently of the new helper.
No camera, native command submission, or published firmware bytes are written.
"""
from pathlib import Path
import hashlib, io, json, struct, subprocess, sys

W = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(W / 'wb-load-p42/deps'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_LR, UC_ARM_REG_SP, UC_ARM_REG_PC

IMAGE = W / 'demo-p49/research/backend-reuse/camera-runtime-temp.ext4'
APP_SHA = '55dcc78143724435a2e38b9e0d809960bd728668634bbcf7532ce137f882eb04'
WAIT, COMPLETE = 0x1e8598, 0x1e827c
EXPECTED, SEM_STORAGE = 0x4aa52c, 0x4aa530
SEM_WAIT, SEM_POST = 0xadce4, 0xad9c0
STACK, RETURN = 0x70000000, 0x71000000


class OriginalHandshake:
    def __init__(self, raw):
        self.u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        elf = ELFFile(io.BytesIO(raw))
        mapped = set()
        for seg in elf.iter_segments():
            if seg['p_type'] != 'PT_LOAD':
                continue
            lo = seg['p_vaddr'] & ~4095
            hi = (seg['p_vaddr'] + seg['p_memsz'] + 4095) & ~4095
            for address in range(lo, hi, 4096):
                if address not in mapped:
                    self.u.mem_map(address, 4096)
                    mapped.add(address)
            self.u.mem_write(seg['p_vaddr'], seg.data())
        self.u.mem_map(STACK, 0x100000)
        self.u.mem_map(RETURN, 4096)
        self.u.mem_write(SEM_STORAGE, struct.pack('<I', STACK + 0x100))
        self.tokens = self.posts = self.consumed = 0
        self.blocked = self.returned = False
        self.u.hook_add(UC_HOOK_CODE, self.hook)

    def word(self, address):
        return struct.unpack('<I', self.u.mem_read(address, 4))[0]

    def hook(self, u, address, size, unused):
        if address == RETURN:
            self.returned = True
            u.emu_stop()
        elif address == SEM_WAIT:
            assert u.reg_read(UC_ARM_REG_R0) == self.word(SEM_STORAGE)
            if not self.tokens:
                self.blocked = True
                u.emu_stop()
                return
            self.tokens -= 1
            self.consumed += 1
            u.reg_write(UC_ARM_REG_R0, 0)
            u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))
        elif address == SEM_POST:
            assert u.reg_read(UC_ARM_REG_R0) == self.word(SEM_STORAGE)
            self.tokens += 1
            self.posts += 1
            u.reg_write(UC_ARM_REG_R0, 0)
            u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))
        elif not COMPLETE <= address < 0x1e8760:
            # All other callees on these paths format diagnostic text or
            # resolve callback names. They do not authorize completion.
            u.reg_write(UC_ARM_REG_R0, 0)
            u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))

    def call(self, entry, callback, stack_offset=0x80000):
        self.blocked = self.returned = False
        self.u.reg_write(UC_ARM_REG_R0, callback)
        self.u.reg_write(UC_ARM_REG_SP, STACK + stack_offset)
        self.u.reg_write(UC_ARM_REG_LR, RETURN)
        self.u.emu_start(entry, RETURN + 4, count=20000)
        assert self.blocked or self.returned

    def resume_wait(self, context):
        self.u.context_restore(context)
        self.blocked = self.returned = False
        self.u.emu_start(self.u.reg_read(UC_ARM_REG_PC), RETURN + 4, count=20000)
        assert self.blocked or self.returned


def main():
    raw = subprocess.run(
        ['debugfs', '-R', 'cat /usr/apps/com.samsung.di-camera-app/bin/di-camera-app', str(IMAGE)],
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    assert hashlib.sha256(raw).hexdigest() == APP_SHA
    cases = []

    m = OriginalHandshake(raw)
    m.call(WAIT, 8)
    assert m.blocked and m.word(EXPECTED) == 8
    waiter = m.u.context_save()
    m.call(COMPLETE, 8, 0x90000)
    assert m.returned and m.word(EXPECTED) == 0 and m.posts == 1
    m.resume_wait(waiter)
    assert m.returned and m.consumed == 1 and m.tokens == 0
    cases.append('actual_wait_then_complete_returns_normally')

    m = OriginalHandshake(raw)
    m.call(COMPLETE, 8)
    assert m.returned and m.posts == 0 and m.word(EXPECTED) == 0
    m.call(WAIT, 8)
    assert m.blocked and m.word(EXPECTED) == 8 and m.tokens == 0
    cases.append('actual_complete_before_wait_is_dropped_then_wait_blocks')

    m = OriginalHandshake(raw)
    m.call(WAIT, 8)
    first = m.u.context_save()
    m.call(WAIT, 9, 0x90000)
    assert m.blocked and m.word(EXPECTED) == 9
    m.call(COMPLETE, 8, 0xa0000)
    assert m.returned and m.posts == 0 and m.word(EXPECTED) == 9
    m.call(COMPLETE, 9, 0xa0000)
    assert m.posts == 1 and m.word(EXPECTED) == 0
    # A shared POSIX semaphore may wake either waiter. The old start waiter
    # can therefore return using a stop completion: no per-waiter binding.
    m.resume_wait(first)
    assert m.returned and m.consumed == 1
    cases.append('actual_concurrent_wait_overwrites_expected_and_can_wake_wrong_waiter')

    m = OriginalHandshake(raw)
    m.call(COMPLETE, 8)
    m.call(COMPLETE, 8)
    assert m.returned and m.posts == 0 and m.tokens == 0 and m.word(EXPECTED) == 0
    cases.append('actual_no_wait_callback_path_creates_no_residual_semaphore_token')

    report = {
        'format': 'NX51_OEM_ARM_HANDSHAKE_REVIEW1', 'passed': True,
        'actual_original_ARM_instructions': True, 'original_app_sha256': APP_SHA,
        'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'cases': cases, 'tests_run': len(cases), 'hardware_tested': False,
        'native_commands_submitted': 0,
        'models': ['diagnostic logging callees', 'POSIX semaphore tokens and wake selection'],
        'limits': ['No claim of camera scheduling or native liveview recovery success.'],
    }
    path = Path(__file__).resolve().parents[1] / 'results/original-ui-wait-race.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()

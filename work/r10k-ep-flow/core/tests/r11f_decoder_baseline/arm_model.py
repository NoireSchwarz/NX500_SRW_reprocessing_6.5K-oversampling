"""Execute static ARM Linux test ELFs with Unicorn and a small modeled OS.

Only test stdio, anonymous memory and clock syscalls are modeled. This is
instruction-level validation, never a camera, MMIO, kernel or ptrace test.
Unknown syscalls stop execution instead of silently succeeding.
"""
from pathlib import Path
import struct, sys

sys.path.insert(0, str(Path(__file__).resolve().parent / 'tooling/python'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_INTR, UcError
from unicorn.arm_const import *


class ArmModel:
    def __init__(self, path, args=()):
        self.path = Path(path)
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self.output = bytearray()
        self.exit_code = None
        self.heap = 0x20000000
        self.map_next = 0x30000000
        self.clock_ns = 10_000_000_000
        self.calls = {}
        self.error = None
        with self.path.open('rb') as f:
            elf = ELFFile(f)
            assert elf['e_machine'] == 'EM_ARM' and elf.elfclass == 32
            loads = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
            begin = min(s['p_vaddr'] for s in loads) & ~4095
            end = (max(s['p_vaddr'] + s['p_memsz'] for s in loads) + 4095) & ~4095
            self.uc.mem_map(begin, end - begin)
            for s in loads:
                self.uc.mem_write(s['p_vaddr'], s.data())
            entry = elf['e_entry']
            header = next(s['p_vaddr'] - s['p_offset'] for s in loads if s['p_offset'] == 0)
            aux = [(3, header + elf['e_phoff']), (4, elf['e_phentsize']), (5, elf['e_phnum']),
                   (6, 4096), (7, 0), (9, entry), (11, 0), (12, 0), (13, 0), (14, 0),
                   (16, 0), (17, 100), (23, 0)]
        self.uc.mem_map(0x7f000000, 0x200000)
        cursor = 0x7f1ff000
        arg_ptrs = []
        for arg in [str(self.path), *args]:
            data = str(arg).encode() + b'\0'
            cursor -= len(data)
            self.uc.mem_write(cursor, data)
            arg_ptrs.append(cursor)
        cursor = (cursor - 32) & ~15
        self.uc.mem_write(cursor, bytes(range(16)))
        aux += [(25, cursor), (0, 0)]
        words = [len(arg_ptrs), *arg_ptrs, 0, 0] + [v for pair in aux for v in pair]
        sp = (cursor - 4 * len(words)) & ~15
        self.uc.mem_write(sp, struct.pack('<' + 'I' * len(words), *words))
        self.uc.reg_write(UC_ARM_REG_SP, sp)
        self.uc.mem_map(0xffff0000, 4096)
        # Linux ARM kuser TLS and memory-barrier helpers.
        self.uc.mem_write(0xffff0fe0, struct.pack('<II', 0xee1d0f70, 0xe12fff1e))
        self.uc.mem_write(0xffff0fa0, struct.pack('<II', 0xf57ff05b, 0xe12fff1e))
        self.uc.mem_write(0xffff0ffc, struct.pack('<I', 5))
        self.uc.mem_map(self.heap, 0x1000000)
        self.uc.hook_add(UC_HOOK_INTR, self.interrupt)
        self.entry = entry

    def ret(self, value):
        self.uc.reg_write(UC_ARM_REG_R0, value & 0xffffffff)

    def interrupt(self, uc, number, data):
        n = uc.reg_read(UC_ARM_REG_R7)
        a = [uc.reg_read(r) for r in [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                                      UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5]]
        self.calls[n] = self.calls.get(n, 0) + 1
        if n in (1, 248):
            self.exit_code = a[0]
            uc.emu_stop()
        elif n == 4:
            assert a[0] in (1, 2)
            self.output.extend(uc.mem_read(a[1], a[2])); self.ret(a[2])
        elif n == 146:
            assert a[0] in (1, 2)
            total = 0
            for i in range(a[2]):
                p, length = struct.unpack('<II', uc.mem_read(a[1] + 8*i, 8))
                self.output.extend(uc.mem_read(p, length)); total += length
            self.ret(total)
        elif n in (20, 224, 256): self.ret(123)
        elif n in (24, 47, 49, 50, 199, 200, 201, 202): self.ret(0)
        elif n == 45:
            if a[0]: self.heap = a[0]
            self.ret(self.heap)
        elif n == 192:
            assert a[3] & 0x20, 'only anonymous mmap is modeled'
            length = (a[1] + 4095) & ~4095
            address = self.map_next
            self.map_next += length + 4096
            uc.mem_map(address, length)
            self.ret(address)
        elif n in (91, 125, 174, 175): self.ret(0)
        elif n == 54: self.ret(-25)
        elif n == 263:
            uc.mem_write(a[1], struct.pack('<ii', self.clock_ns // 10**9, self.clock_ns % 10**9)); self.ret(0)
        elif n == 162:
            sec, nano = struct.unpack('<ii', uc.mem_read(a[0], 8))
            self.clock_ns += sec * 10**9 + nano; self.ret(0)
        elif n == 0xf0005:
            uc.reg_write(UC_ARM_REG_C13_C0_3, a[0]); self.ret(0)
        elif n in (281,282,290,292):
            # No optional stage viewer in the default CPU/metadata fixture.
            # Dedicated stage-channel tests model these local socket calls.
            self.ret(-97)
        elif n in (403, 407, 398): self.ret(-38)
        elif n == 384:
            uc.mem_write(a[0], bytes((i*31+7) & 255 for i in range(a[1]))); self.ret(a[1])
        else:
            self.error = f'unsupported syscall {n} args={a}'
            uc.emu_stop()

    def run(self):
        try:
            self.uc.emu_start(self.entry, 0, timeout=30_000_000, count=300_000_000)
        except UcError as e:
            raise RuntimeError(f'{self.path.name}: {e}; pc={self.uc.reg_read(UC_ARM_REG_PC):08x}; calls={self.calls}; output={self.output.decode(errors="replace")}') from e
        if self.error or self.exit_code is None:
            raise RuntimeError(f'{self.error or "ARM execution limit exceeded"}; calls={self.calls}; output={self.output.decode(errors="replace")}')
        return {'exit': self.exit_code, 'stdout': self.output.decode(errors='replace'),
                'modeled_syscalls': self.calls, 'hardware_tested': False}


if __name__ == '__main__':
    import json
    result = ArmModel(sys.argv[1], sys.argv[2:]).run()
    print(json.dumps(result, indent=2))
    sys.exit(result['exit'])

"""Exercise the YuNet region policy and empty-frame rejection on Cortex-A9."""

from pathlib import Path
import struct
import sys

from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP
from replay import OWNER, R0, STOP, arm_elf


def smoke(image):
    cpu, symbols = arm_elf(image)

    def call(name, arg=0):
        cpu.reg_write(UC_ARM_REG_SP, OWNER + 0xfff0)
        cpu.reg_write(UC_ARM_REG_LR, STOP)
        cpu.reg_write(R0, arg)
        cpu.emu_start(symbols[name], STOP, count=64000000)
        assert cpu.reg_read(UC_ARM_REG_PC) == STOP, name
        return cpu.reg_read(R0)

    assert call("verify_priority") == 0
    geometry_check = call("verify_focus_geometry")
    assert geometry_check == 0, f"Focus geometry check {geometry_check}"
    cpu.mem_write(symbols["ordinary"], struct.pack("<4i", 100, 90, 96, 108))
    cpu.mem_write(symbols["preview_pixels"], bytes([128]) * (528 * 352 * 3 // 2))
    assert call("verify_points") == 1
    assert call("verify_points", 181) == 1
    assert cpu.mem_read(symbols["found_points"], 24) == bytes(24)
    rate_check = call("verify_rate")
    assert rate_check == 0, f"Rate recovery check {rate_check}"


if __name__ == "__main__":
    for elf in sys.argv[1:]:
        smoke(Path(elf))
    print("YuNet scalar and NEON: eye priority, focus geometry, empty frame and rate recovery checks passed.")

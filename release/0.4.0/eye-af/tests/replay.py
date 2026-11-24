"""Run the ARM face/eye fixtures. The camera endpoints and clock are modeled."""
import hashlib
import io
import json
import struct
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0 as R0,
    UC_ARM_REG_SP, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_CPU_ARM_CORTEX_A9)

OWNER = 0x90000000
STOP = 0x08000000
MEMBER = "_ZN20CFaceDetection_Linux13FaceDetectionEPhiiii"
WORD, BOX = struct.Struct("<I"), struct.Struct("<4i")

def arm_elf(path):
    elf = ELFFile(io.BytesIO(path.read_bytes()))
    cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
    cpu.reg_write(UC_ARM_REG_C1_C0_2, 0x00f00000)
    cpu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    loads = [s for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]
    lo = min(s["p_vaddr"] for s in loads) & ~4095
    hi = (max(s["p_vaddr"]+s["p_memsz"] for s in loads)+4095) & ~4095
    cpu.mem_map(lo, hi-lo)
    for s in loads:
        cpu.mem_write(s["p_vaddr"], s.data())
    cpu.mem_map(OWNER, 0x10000)
    return cpu, {s.name:s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}


SCENARIOS = ("stable", "missed_eye", "lost_face", "eyes_hidden", "multiple", "face_leaves",
             "repeated_gaps", "return_without_eye", "face_mode_off", "two_misses", "fast",
             "late_233_ms", "late_333_ms", "expires_during_probe", "face_moves", "blip18", "blip19",
             "width_blip", "flag_blip", "one_multi", "jump_with_eye", "blip_then_hidden", "smooth_pan",
             "smooth_zoom", "fast_pan", "native_eye_moves", "repeated_jumps", "primary_blip", "size_blip",
             "stock_error", "stale_eye_coords", "motion_after_loss", "motion_with_hidden_eyes", "native_on_jump",
             "jump_then_miss", "other_eye", "dense_zeros", "double_zero", "long_absence", "gap_then_multiple",
             "gap_then_other_hidden", "gap_miss_near_sample", "native_on_return", "recent_probe_gap",
             "zero_after_frame_change", "zero_after_stock_box", "zero_after_buffer_change",
             "aaf_eye_returns", "aaf_eye_returns_late", "aaf_eye_stays_hidden", "aaf_subject_changes",
             "aaf_multiple_after_expiry", "aaf_lost_after_expiry", "aaf_never_saw_eye", "aaf_real_eye_at_expiry",
             "aaf_peak_eye_returns", "aaf_peak_eye_hidden")
WAITS = ("wait_first", "wait_geom", "wait_valid", "wait_no_face", "wait_multi", "wait_gate", "wait_bounds",
         "wait_reset", "wait_expired", "wait_no_eye")
EVENTS = ("jumps", "invalid_flags", "motion_frames", "return_tries", "return_hits", "move_tries", "move_hits",
          "cooldown_frames", "face_returns", "gate_releases")
GAPS = ("gap_returns", "gap_first_hits", "one_zero_returns", "longest_zero_run", "longest_return_ms",
        "gap_probes", "gap_hits", "gap_budget_waits", "gap_spacing_waits",
        "prior_unknown", "prior_face", "prior_changed", "prior_eye_seq")


def run_case(raw, scenario):
    from elftools.elf.elffile import ELFFile
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
    from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_PC,
                                   UC_ARM_REG_R0, UC_ARM_REG_SP, UC_CPU_ARM_CORTEX_A9)
    elf = ELFFile(io.BytesIO(raw))
    segments = [s for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]
    cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
    cpu.reg_write(UC_ARM_REG_C1_C0_2, 0x00f00000)
    cpu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    base = min(s["p_vaddr"] for s in segments) & ~4095
    end = (max(s["p_vaddr"] + s["p_memsz"] for s in segments) + 4095) & ~4095
    cpu.mem_map(base, end - base)
    for seg in segments:
        cpu.mem_write(seg["p_vaddr"], seg.data())
    cpu.mem_map(0x07000000, 0x10000)
    stop = 0x08000000
    cpu.reg_write(UC_ARM_REG_SP, 0x0700fff0)
    cpu.reg_write(UC_ARM_REG_LR, stop)
    symbols = elf.get_section_by_name(".symtab")
    addr = lambda name: symbols.get_symbol_by_name(name)[0]["st_value"]
    cpu.reg_write(UC_ARM_REG_R0, scenario)
    cpu.emu_start(addr("study_dropout"), stop, count=3000000)
    assert cpu.reg_read(UC_ARM_REG_PC) == stop and cpu.reg_read(UC_ARM_REG_R0) == 0
    frames = struct.unpack("<90I", cpu.mem_read(addr("dropout_frames"), 360))
    assert not any(flags & 4 and not flags & 2 for flags in frames)
    assert not any(flags & 64 and flags & (2 | 4 | 8) for flags in frames)
    counters = struct.unpack("<5I", cpu.mem_read(addr("dropout_counters"), 20))
    losses = struct.unpack("<20I", cpu.mem_read(addr("dropout_losses"), 80))
    gaps = struct.unpack("<13I", cpu.mem_read(addr("dropout_gap_counters"), 52))
    relock = struct.unpack("<2I", cpu.mem_read(addr("dropout_relock_counters"), 8))
    errors = struct.unpack("<90I", cpu.mem_read(addr("dropout_errors"), 360))
    geometry = list(struct.iter_unpack("<4i", cpu.mem_read(addr("dropout_geometry"), 90 * 16)))
    selected = [box for box, f in zip(geometry, frames) if f & 4]
    return {"eligible": sum(bool(f & 2) for f in frames), "eye_frames": sum(bool(f & 4) for f in frames),
            "face_only_frames": [i for i,f in enumerate(frames) if f & 2 and not f & 4],
            "back_to_face": [i for i in range(1, len(frames)) if frames[i-1] & 4 and frames[i] & 2 and not frames[i] & 4],
            "max_center_error_px": max(errors),
            "x_span_px": max(b[0] for b in selected) - min(b[0] for b in selected) if selected else 0,
            "roi_width_span_px": max(b[2] for b in selected) - min(b[2] for b in selected) if selected else 0,
            "calls_at": [i for i,f in enumerate(frames) if f & 8],
            "early_at": [i for i,f in enumerate(frames) if f & 32],
            "zero_at": [i for i,f in enumerate(frames) if f & 64],
            "timeouts": sum(bool(f & 16) for f in frames),
            **dict(zip(WAITS + EVENTS, losses)),
            **dict(zip(GAPS, gaps)),
            **dict(zip(("relock_probes", "relock_hits"), relock)),
            **dict(zip(("probe_misses", "early_tries", "early_hits", "face_gaps", "extended_hold_frames"), counters))}


def stable(image, expected):
    cpu, s = arm_elf(image)
    rows, returns = [], set()
    def box(ptr):
        return BOX.unpack(cpu.mem_read(ptr, BOX.size))
    def record(key):
        rows[-1][key] = box(s["data"])
    def after(key):
        addr = cpu.reg_read(UC_ARM_REG_LR)
        if (addr, key) not in returns:
            cpu.hook_add(UC_HOOK_CODE, lambda *_: record(key), begin=addr, end=addr)
            returns.add((addr, key))
    def enter(*_):
        rows.append({"writes_to_primary":0})
        record("entry")
        after("final")
    def native(*_):
        private = WORD.unpack(cpu.mem_read(cpu.reg_read(R0) + 0x78, 4))[0]
        rows[-1]["native_input"] = box(private)
        record("during")
        after("after_native")
    def aaf(*_):
        if cpu.reg_read(R0) == 0xe3:
            cpu.reg_write(R0, 2)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
    def write(*_):
        if rows:
            rows[-1]["writes_to_primary"] += 1
    for name, hook in ((MEMBER, enter), ("scripted_face", native), ("camera_setting", aaf)):
        cpu.hook_add(UC_HOOK_CODE, hook, begin=s[name], end=s[name])
    cpu.hook_add(UC_HOOK_MEM_WRITE, write, begin=s["data"], end=s["data"]+15)
    cpu.reg_write(UC_ARM_REG_SP, 0x9000fff0)
    cpu.reg_write(UC_ARM_REG_LR, STOP)
    cpu.reg_write(R0, 0)
    cpu.emu_start(s["study_dropout"], STOP, count=4000000)
    assert cpu.reg_read(UC_ARM_REG_PC) == STOP and cpu.reg_read(R0) == 0 and len(rows) == 90
    flags = struct.unpack("<90I", cpu.mem_read(s["dropout_frames"], 360))
    assert all(f & 4 for f in flags)
    eye, face = rows[0]["final"], rows[0]["native_input"]
    assert eye != face and all(r["native_input"] == face and r["final"] == eye for r in rows)
    exposed = sum(r["entry"] == eye and face in (r["during"], r["after_native"]) for r in rows[1:])
    assert exposed == expected
    if not expected:
        assert all(r["entry"] == r["during"] == r["after_native"] == eye and not r["writes_to_primary"]
                   for r in rows[1:])
    return {"sha256":hashlib.sha256(image.read_bytes()).hexdigest(), "frames":len(rows), "final_eyes":90, "transient_full_faces":exposed,
            "frames_writing_stable_primary":sum(r["writes_to_primary"] != 0 for r in rows[1:]),
            "example":rows[1]}



if __name__ == "__main__":
    if not __debug__:
        raise SystemExit("Run Python with assertions enabled.")
    image = Path(sys.argv[1])
    pins = json.loads(Path(__file__).with_name("scenario-sha256.json").read_text("utf8"))
    raw = image.read_bytes()
    assert set(pins) == set(SCENARIOS)
    for i, name in enumerate(SCENARIOS):
        result = run_case(raw, i)
        digest = hashlib.sha256(json.dumps(result, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        assert digest == pins[name], (name, result)
    result = stable(image, 0)
    print(f"{len(pins)} ARM scenarios matched. Stable sequence: {result['frames']} eye results, "
          f"{result['transient_full_faces']} temporary full-face exposures.")

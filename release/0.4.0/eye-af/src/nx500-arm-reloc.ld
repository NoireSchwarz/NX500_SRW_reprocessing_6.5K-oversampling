/* The NX500 loader combines DT_REL and DT_JMPREL during eager binding.
   Keep the unwind table outside that contiguous relocation range. */
SECTIONS {
    .ARM.exidx : { *(.ARM.exidx*) }
} INSERT BEFORE .rel.dyn;

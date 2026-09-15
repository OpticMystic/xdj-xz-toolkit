/* The XZ's glibc 2.13 applies DT_RELCOUNT to each eager relocation range.
 * Keep PLT relocations adjacent so it processes one combined range. */
SECTIONS { .rel.plt : { *(.rel.plt) } } INSERT AFTER .rel.dyn;

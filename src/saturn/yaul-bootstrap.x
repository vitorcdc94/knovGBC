/*
 * Yaul's standard linker layout, with the custom bootstrap fixed at the
 * 1ST_READ address before all ordinary .text input sections.
 */
OUTPUT_FORMAT ("elf32-sh")
OUTPUT_ARCH (sh)
EXTERN (_start)
ENTRY (_start)
SEARCH_DIR ("$YAUL_INSTALL_ROOT/$YAUL_ARCH_SH_PREFIX/lib");

MEMORY {
  ram (Wx) : ORIGIN = 0x06004000, LENGTH = 0x000FC000
}

SECTIONS
{
  .text :
  {
     KEEP(*(.text.start))
     *(.text)
     *(.text.*)
     *(.gnu.linkonce.t.*)

     /* Candidate-only cold SM83 helpers use a section name outside the
      * generic .text.* wildcard so they cannot be interleaved with the
      * decoder's normal instruction path.  Builds without such helpers are
      * unchanged. */
     *(.knovgbc.coldtext*)

     INCLUDE ldscripts/yaul-c++.x

     . = ALIGN (4);
  }

  .rodata :
  {
     . = ALIGN (16);

     *(.rdata)
     *(.rodata)
     *(.rodata.*)
     *(.gnu.linkonce.r.*)
  }

  .data :
  {
     . = ALIGN (16);

     *(.data)
     *(.data.*)
     *(.gnu.linkonce.d.*)
     SORT (CONSTRUCTORS)
     *(.sdata)
     *(.sdata.*)
     *(.gnu.linkonce.s.*)
  }

  .bss :
  {
     . = ALIGN (16);
     PROVIDE (___bss_start = .);

     *(.bss)
     *(.bss.*)
     *(.gnu.linkonce.b.*)
     *(.sbss)
     *(.sbss.*)
     *(.gnu.linkonce.sb.*)
     *(.scommon)
     *(COMMON)

     . = ALIGN (16);
     PROVIDE (___bss_end = .);
  }

  .uncached (0x20000000 | ___bss_end) : AT (___bss_end)
  {
     *(.uncached)
     *(.uncached.*)

     . = ALIGN (4);
  }

  ___end = ___bss_end + SIZEOF (.uncached);
}

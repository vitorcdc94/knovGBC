/* Linker script for the standalone SH-2 boot probe.
 *
 * The Saturn system program transfers the 1ST_READ program to 0x06004000.
 * Keep this entirely separate from Yaul's default linker script and CRT.
 */
OUTPUT_FORMAT("elf32-sh")
OUTPUT_ARCH(sh)
ENTRY(_start)

MEMORY
{
    ram (rwx) : ORIGIN = 0x06004000, LENGTH = 0x000FC000
}

SECTIONS
{
    . = ORIGIN(ram);

    .text.start :
    {
        KEEP(*(.text.start))
    } > ram

    .text :
    {
        *(.text)
        *(.text.*)
    } > ram

    .rodata :
    {
        *(.rodata)
        *(.rodata.*)
    } > ram

    .data :
    {
        *(.data)
        *(.data.*)
    } > ram

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}

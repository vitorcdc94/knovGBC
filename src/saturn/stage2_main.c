#include <yaul.h>

/*
 * The first instruction after the C runtime calls main is deliberately not a
 * valid SH-2 opcode.  Yaul's already-linked illegal-instruction handler must
 * render its green diagnostic page.  The reported PC is expected to point to
 * this file's main function, which distinguishes a boot handoff failure from
 * ordinary VDP2 output setup.
 */
int
main(void)
{
    __asm__ volatile (".word 0xFFFF");

    for (;;) {
    }
}

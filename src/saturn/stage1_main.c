#include <yaul.h>

/*
 * This repeats the complete Yaul error-console path, except that it prints a
 * positive execution marker.  It is intentionally conservative: the earlier
 * green exception page has already proven that this sequence is visible on
 * this console and television.
 */
extern void __reset(void);
extern void __vdp2_commit(uint32_t level);
extern void __vdp2_commit_wait(uint32_t level);

int
main(void)
{
    __reset();
    dbgio_init();
    dbgio_dev_deinit();
    dbgio_dev_default_init(DBGIO_DEV_VDP2);
    vdp2_tvmd_vblank_in_next_wait(1);
    dbgio_dev_font_load();
    dbgio_puts("\x1B[H\x1B[2JSTAGE 1: MAIN EXECUTING\n");
    dbgio_puts("Bootstrap and Yaul console are working.\n");
    vdp2_tvmd_vblank_in_next_wait(1);
    dbgio_flush();
    __vdp2_commit(0);
    __vdp2_commit_wait(0);

    for (;;) {
    }
}

#include "platform.h"

#include "gb.h"

#include <yaul.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define ROM_ENTRY_LIMIT 128

/*
 * H-WRAM holds the program, Yaul and SameBoy's working state. The whole
 * 1 MiB L-WRAM is reserved for the loaded cartridge image, which prevents a
 * large ROM from exhausting H-WRAM before SameBoy has a chance to start.
 */
#define ROM_SIZE_LIMIT LWRAM_SIZE

static uint8_t *const rom_storage = (uint8_t *)LWRAM(0);

static bool fast_forward;
static bool cd_ready;

static bool
is_rom_name(const char *name)
{
    const size_t length = strlen(name);

    if (length < 3) {
        return false;
    }

    return (strcmp(name + length - 3, ".GB") == 0) ||
        ((length >= 4) && (strcmp(name + length - 4, ".GBC") == 0));
}

static void
copy_title(char *title, uint32_t title_size, const char *name)
{
    if ((title == NULL) || (title_size == 0)) {
        return;
    }

    uint32_t index = 0;
    while ((index + 1U < title_size) && (name[index] != '\0')) {
        title[index] = name[index];
        index++;
    }
    title[index] = '\0';
}

void
saturn_platform_init(void)
{
    saturn_platform_video_init();
    (void)saturn_platform_services_init();
}

bool
saturn_platform_services_init(void)
{
    fast_forward = false;
    cd_ready = false;

    smpc_peripheral_init();
    smpc_peripheral_intback_issue();

    if (cd_block_init() == 0) {
        cdfs_init();
        cd_ready = true;
    }

    return cd_ready;
}

void
saturn_platform_update_input(GB_gameboy_t *gb)
{
    smpc_peripheral_digital_t digital = { 0 };
    GB_key_mask_t keys = 0;

    smpc_peripheral_process();
    smpc_peripheral_digital_port(1, &digital);
    smpc_peripheral_intback_issue();

    if (digital.connected) {
        if (digital.pressed.button.right) {
            keys |= GB_KEY_RIGHT_MASK;
        }
        if (digital.pressed.button.left) {
            keys |= GB_KEY_LEFT_MASK;
        }
        if (digital.pressed.button.up) {
            keys |= GB_KEY_UP_MASK;
        }
        if (digital.pressed.button.down) {
            keys |= GB_KEY_DOWN_MASK;
        }
        if (digital.pressed.button.a) {
            keys |= GB_KEY_A_MASK;
        }
        if (digital.pressed.button.b) {
            keys |= GB_KEY_B_MASK;
        }
        if (digital.pressed.button.l) {
            keys |= GB_KEY_SELECT_MASK;
        }
        if (digital.pressed.button.start) {
            keys |= GB_KEY_START_MASK;
        }
        fast_forward = digital.pressed.button.r;
    } else {
        fast_forward = false;
    }

    GB_set_key_mask(gb, keys);
}

bool
saturn_platform_fast_forward(void)
{
    return fast_forward;
}

bool
saturn_platform_load_selected_rom(uint8_t **rom, uint32_t *size,
    char *title, uint32_t title_size)
{
    if ((rom == NULL) || (size == NULL) || !cd_ready) {
        return false;
    }

    *rom = NULL;
    *size = 0;
    copy_title(title, title_size, "");

    cdfs_filelist_entry_t *const entries = cdfs_entries_alloc(ROM_ENTRY_LIMIT);
    if (entries == NULL) {
        return false;
    }

    cdfs_filelist_t filelist;
    cdfs_filelist_init(&filelist, entries, ROM_ENTRY_LIMIT);
    cdfs_filelist_root_read(&filelist);

    const cdfs_filelist_entry_t *rom_directory = NULL;
    for (uint32_t index = 0; index < filelist.entries_count; index++) {
        const cdfs_filelist_entry_t *const entry = &filelist.entries[index];
        if ((entry->type == CDFS_ENTRY_TYPE_DIRECTORY) &&
            (strcmp(entry->name, "ROMS") == 0)) {
            rom_directory = entry;
            break;
        }
    }

    if (rom_directory == NULL) {
        cdfs_entries_free(entries);
        return false;
    }

    cdfs_filelist_init(&filelist, entries, ROM_ENTRY_LIMIT);
    cdfs_filelist_read(&filelist, *rom_directory);

    const cdfs_filelist_entry_t *rom_entry = NULL;
    for (uint32_t index = 0; index < filelist.entries_count; index++) {
        const cdfs_filelist_entry_t *const entry = &filelist.entries[index];
        if ((entry->type == CDFS_ENTRY_TYPE_FILE) && is_rom_name(entry->name)) {
            rom_entry = entry;
            break;
        }
    }

    if ((rom_entry == NULL) || (rom_entry->size == 0) ||
        (rom_entry->size > ROM_SIZE_LIMIT)) {
        cdfs_entries_free(entries);
        return false;
    }

    /* cd_block_sectors_read uses programmed I/O, so L-WRAM is safe here. */
    if (cd_block_sectors_read(rom_entry->starting_fad, rom_storage,
        rom_entry->size) != 0) {
        cdfs_entries_free(entries);
        return false;
    }

    copy_title(title, title_size, rom_entry->name);
    *rom = rom_storage;
    *size = rom_entry->size;
    cdfs_entries_free(entries);
    return true;
}

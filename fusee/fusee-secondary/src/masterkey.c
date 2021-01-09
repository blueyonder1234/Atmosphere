/*
 * Copyright (c) 2018-2020 Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <vapours/ams/ams_target_firmware.h>

#include "utils.h"
#include "masterkey.h"
#include "se.h"
#include "fuse.h"

static unsigned int g_mkey_revision = 0;
static bool g_determined_mkey_revision = false;

static uint8_t g_old_masterkeys[MASTERKEY_REVISION_MAX][0x10];
static uint8_t g_old_devicekeys[MASTERKEY_NUM_NEW_DEVICE_KEYS - 1][0x10];

/* TODO: Extend with new vectors, as needed. */
/* Dev unit keys. */
static const uint8_t mkey_vectors_dev[MASTERKEY_REVISION_MAX][0x10] = {
    {0x46, 0x22, 0xB4, 0x51, 0x9A, 0x7E, 0xA7, 0x7F, 0x62, 0xA1, 0x1F, 0x8F, 0xC5, 0x3A, 0xDB, 0xFE}, /* Zeroes encrypted with Master Key 00. */
    {0x39, 0x33, 0xF9, 0x31, 0xBA, 0xE4, 0xA7, 0x21, 0x2C, 0xDD, 0xB7, 0xD8, 0xB4, 0x4E, 0x37, 0x23}, /* Master key 00 encrypted with Master key 01. */
    {0x97, 0x29, 0xB0, 0x32, 0x43, 0x14, 0x8C, 0xA6, 0x85, 0xE9, 0x5A, 0x94, 0x99, 0x39, 0xAC, 0x5D}, /* Master key 01 encrypted with Master key 02. */
    {0x2C, 0xCA, 0x9C, 0x31, 0x1E, 0x07, 0xB0, 0x02, 0x97, 0x0A, 0xD8, 0x03, 0xA2, 0x76, 0x3F, 0xA3}, /* Master key 02 encrypted with Master key 03. */
    {0x9B, 0x84, 0x76, 0x14, 0x72, 0x94, 0x52, 0xCB, 0x54, 0x92, 0x9B, 0xC4, 0x8C, 0x5B, 0x0F, 0xBA}, /* Master key 03 encrypted with Master key 04. */
    {0x78, 0xD5, 0xF1, 0x20, 0x3D, 0x16, 0xE9, 0x30, 0x32, 0x27, 0x34, 0x6F, 0xCF, 0xE0, 0x27, 0xDC}, /* Master key 04 encrypted with Master key 05. */
    {0x6F, 0xD2, 0x84, 0x1D, 0x05, 0xEC, 0x40, 0x94, 0x5F, 0x18, 0xB3, 0x81, 0x09, 0x98, 0x8D, 0x4E}, /* Master key 05 encrypted with Master key 06. */
    {0x37, 0xAF, 0xAB, 0x35, 0x79, 0x09, 0xD9, 0x48, 0x29, 0xD2, 0xDB, 0xA5, 0xA5, 0xF5, 0x30, 0x19}, /* Master key 06 encrypted with Master key 07. */
    {0xEC, 0xE1, 0x46, 0x89, 0x37, 0xFD, 0xD2, 0x15, 0x8C, 0x3F, 0x24, 0x82, 0xEF, 0x49, 0x68, 0x04}, /* Master key 07 encrypted with Master key 08. */
    {0x43, 0x3D, 0xC5, 0x3B, 0xEF, 0x91, 0x02, 0x21, 0x61, 0x54, 0x63, 0x8A, 0x35, 0xE7, 0xCA, 0xEE}, /* Master key 08 encrypted with Master key 09. */
    {0x6C, 0x2E, 0xCD, 0xB3, 0x34, 0x61, 0x77, 0xF5, 0xF9, 0xB1, 0xDD, 0x61, 0x98, 0x19, 0x3E, 0xD4}, /* Master key 09 encrypted with Master key 0A. */
};

/* Retail unit keys. */
static const uint8_t mkey_vectors[MASTERKEY_REVISION_MAX][0x10] = {
    {0x0C, 0xF0, 0x59, 0xAC, 0x85, 0xF6, 0x26, 0x65, 0xE1, 0xE9, 0x19, 0x55, 0xE6, 0xF2, 0x67, 0x3D}, /* Zeroes encrypted with Master Key 00. */
    {0x29, 0x4C, 0x04, 0xC8, 0xEB, 0x10, 0xED, 0x9D, 0x51, 0x64, 0x97, 0xFB, 0xF3, 0x4D, 0x50, 0xDD}, /* Master key 00 encrypted with Master key 01. */
    {0xDE, 0xCF, 0xEB, 0xEB, 0x10, 0xAE, 0x74, 0xD8, 0xAD, 0x7C, 0xF4, 0x9E, 0x62, 0xE0, 0xE8, 0x72}, /* Master key 01 encrypted with Master key 02. */
    {0x0A, 0x0D, 0xDF, 0x34, 0x22, 0x06, 0x6C, 0xA4, 0xE6, 0xB1, 0xEC, 0x71, 0x85, 0xCA, 0x4E, 0x07}, /* Master key 02 encrypted with Master key 03. */
    {0x6E, 0x7D, 0x2D, 0xC3, 0x0F, 0x59, 0xC8, 0xFA, 0x87, 0xA8, 0x2E, 0xD5, 0x89, 0x5E, 0xF3, 0xE9}, /* Master key 03 encrypted with Master key 04. */
    {0xEB, 0xF5, 0x6F, 0x83, 0x61, 0x9E, 0xF8, 0xFA, 0xE0, 0x87, 0xD7, 0xA1, 0x4E, 0x25, 0x36, 0xEE}, /* Master key 04 encrypted with Master key 05. */
    {0x1E, 0x1E, 0x22, 0xC0, 0x5A, 0x33, 0x3C, 0xB9, 0x0B, 0xA9, 0x03, 0x04, 0xBA, 0xDB, 0x07, 0x57}, /* Master key 05 encrypted with Master key 06. */
    {0xA4, 0xD4, 0x52, 0x6F, 0xD1, 0xE4, 0x36, 0xAA, 0x9F, 0xCB, 0x61, 0x27, 0x1C, 0x67, 0x65, 0x1F}, /* Master key 06 encrypted with Master key 07. */
    {0xEA, 0x60, 0xB3, 0xEA, 0xCE, 0x8F, 0x24, 0x46, 0x7D, 0x33, 0x9C, 0xD1, 0xBC, 0x24, 0x98, 0x29}, /* Master key 07 encrypted with Master key 08. */
    {0x4D, 0xD9, 0x98, 0x42, 0x45, 0x0D, 0xB1, 0x3C, 0x52, 0x0C, 0x9A, 0x44, 0xBB, 0xAD, 0xAF, 0x80}, /* Master key 08 encrypted with Master key 09. */
    {0xB8, 0x96, 0x9E, 0x4A, 0x00, 0x0D, 0xD6, 0x28, 0xB3, 0xD1, 0xDB, 0x68, 0x5F, 0xFB, 0xE1, 0x2A}, /* Master key 09 encrypted with Master key 0A. */
};

static const uint8_t new_device_key_sources[MASTERKEY_NUM_NEW_DEVICE_KEYS][0x10] = {
    {0x8B, 0x4E, 0x1C, 0x22, 0x42, 0x07, 0xC8, 0x73, 0x56, 0x94, 0x08, 0x8B, 0xCC, 0x47, 0x0F, 0x5D}, /* 4.x   New Device Key Source. */
    {0x6C, 0xEF, 0xC6, 0x27, 0x8B, 0xEC, 0x8A, 0x91, 0x99, 0xAB, 0x24, 0xAC, 0x4F, 0x1C, 0x8F, 0x1C}, /* 5.x   New Device Key Source. */
    {0x70, 0x08, 0x1B, 0x97, 0x44, 0x64, 0xF8, 0x91, 0x54, 0x9D, 0xC6, 0x84, 0x8F, 0x1A, 0xB2, 0xE4}, /* 6.x   New Device Key Source. */
    {0x8E, 0x09, 0x1F, 0x7A, 0xBB, 0xCA, 0x6A, 0xFB, 0xB8, 0x9B, 0xD5, 0xC1, 0x25, 0x9C, 0xA9, 0x17}, /* 6.2.0 New Device Key Source. */
    {0x8F, 0x77, 0x5A, 0x96, 0xB0, 0x94, 0xFD, 0x8D, 0x28, 0xE4, 0x19, 0xC8, 0x16, 0x1C, 0xDB, 0x3D}, /* 7.0.0 New Device Key Source. */
    {0x67, 0x62, 0xD4, 0x8E, 0x55, 0xCF, 0xFF, 0x41, 0x31, 0x15, 0x3B, 0x24, 0x0C, 0x7C, 0x07, 0xAE}, /* 8.1.0 New Device Key Source. */
    {0x4A, 0xC3, 0x4E, 0x14, 0x8B, 0x96, 0x4A, 0xD5, 0xD4, 0x99, 0x73, 0xC4, 0x45, 0xAB, 0x8B, 0x49}, /* 9.0.0 New Device Key Source. */
    {0x14, 0xB8, 0x74, 0x12, 0xCB, 0xBD, 0x0B, 0x8F, 0x20, 0xFB, 0x30, 0xDA, 0x27, 0xE4, 0x58, 0x94}, /* 9.1.0 New Device Key Source. */
};

static const uint8_t new_device_keygen_sources[MASTERKEY_NUM_NEW_DEVICE_KEYS][0x10] = {
    {0x88, 0x62, 0x34, 0x6E, 0xFA, 0xF7, 0xD8, 0x3F, 0xE1, 0x30, 0x39, 0x50, 0xF0, 0xB7, 0x5D, 0x5D}, /* 4.x   New Device Keygen Source. */
    {0x06, 0x1E, 0x7B, 0xE9, 0x6D, 0x47, 0x8C, 0x77, 0xC5, 0xC8, 0xE7, 0x94, 0x9A, 0xA8, 0x5F, 0x2E}, /* 5.x   New Device Keygen Source. */
    {0x99, 0xFA, 0x98, 0xBD, 0x15, 0x1C, 0x72, 0xFD, 0x7D, 0x9A, 0xD5, 0x41, 0x00, 0xFD, 0xB2, 0xEF}, /* 6.x   New Device Keygen Source. */
    {0x81, 0x3C, 0x6C, 0xBF, 0x5D, 0x21, 0xDE, 0x77, 0x20, 0xD9, 0x6C, 0xE3, 0x22, 0x06, 0xAE, 0xBB}, /* 6.2.0 New Device Keygen Source. */
    {0x86, 0x61, 0xB0, 0x16, 0xFA, 0x7A, 0x9A, 0xEA, 0xF6, 0xF5, 0xBE, 0x1A, 0x13, 0x5B, 0x6D, 0x9E}, /* 7.0.0 New Device Keygen Source. */
    {0xA6, 0x81, 0x71, 0xE7, 0xB5, 0x23, 0x74, 0xB0, 0x39, 0x8C, 0xB7, 0xFF, 0xA0, 0x62, 0x9F, 0x8D}, /* 8.1.0 New Device Keygen Source. */
    {0x03, 0xE7, 0xEB, 0x43, 0x1B, 0xCF, 0x5F, 0xB5, 0xED, 0xDC, 0x97, 0xAE, 0x21, 0x8D, 0x19, 0xED}, /* 9.0.0 New Device Keygen Source. */
    {0xCE, 0xFE, 0x41, 0x0F, 0x46, 0x9A, 0x30, 0xD6, 0xF2, 0xE9, 0x0C, 0x6B, 0xB7, 0x15, 0x91, 0x36}, /* 9.1.0 New Device Keygen Source to be added on next change-of-keys. */
};

static const uint8_t new_device_keygen_sources_dev[MASTERKEY_NUM_NEW_DEVICE_KEYS][0x10] = {
    {0xD6, 0xBD, 0x9F, 0xC6, 0x18, 0x09, 0xE1, 0x96, 0x20, 0x39, 0x60, 0xD2, 0x89, 0x83, 0x31, 0x34}, /* 4.x   New Device Keygen Source. */
    {0x59, 0x2D, 0x20, 0x69, 0x33, 0xB5, 0x17, 0xBA, 0xCF, 0xB1, 0x4E, 0xFD, 0xE4, 0xC2, 0x7B, 0xA8}, /* 5.x   New Device Keygen Source. */
    {0xF6, 0xD8, 0x59, 0x63, 0x8F, 0x47, 0xCB, 0x4A, 0xD8, 0x74, 0x05, 0x7F, 0x88, 0x92, 0x33, 0xA5}, /* 6.x   New Device Keygen Source. */
    {0x20, 0xAB, 0xF2, 0x0F, 0x05, 0xE3, 0xDE, 0x2E, 0xA1, 0xFB, 0x37, 0x5E, 0x8B, 0x22, 0x1A, 0x38}, /* 6.2.0 New Device Keygen Source. */
    {0x60, 0xAE, 0x56, 0x68, 0x11, 0xE2, 0x0C, 0x99, 0xDE, 0x05, 0xAE, 0x68, 0x78, 0x85, 0x04, 0xAE}, /* 7.0.0 New Device Keygen Source. */
    {0x94, 0xD6, 0xA8, 0xC0, 0x95, 0xAF, 0xD0, 0xA6, 0x27, 0x53, 0x5E, 0xE5, 0x8E, 0x70, 0x1F, 0x87}, /* 8.1.0 New Device Keygen Source. */
    {0x61, 0x6A, 0x88, 0x21, 0xA3, 0x52, 0xB0, 0x19, 0x16, 0x25, 0xA4, 0xE3, 0x4C, 0x54, 0x02, 0x0F}, /* 9.0.0 New Device Keygen Source. */
    {0x9D, 0xB1, 0xAE, 0xCB, 0xF6, 0xF6, 0xE3, 0xFE, 0xAB, 0x6F, 0xCB, 0xAF, 0x38, 0x03, 0xFC, 0x7B}, /* 9.1.0 New Device Keygen Source to be added on next change-of-keys. */
};

/* Determine the current SoC for Mariko specific code. */
static bool is_soc_mariko() {
    return (fuse_get_soc_type() == 1);
}

static bool check_mkey_revision(unsigned int revision, bool is_retail) {
    uint8_t final_vector[0x10];

    unsigned int check_keyslot = is_soc_mariko() ? KEYSLOT_SWITCH_MASTERKEY_MARIKO : KEYSLOT_SWITCH_MASTERKEY;
    if (revision > 0) {
        /* Generate old master key array. */
        for (unsigned int i = revision; i > 0; i--) {
            se_aes_ecb_decrypt_block(check_keyslot, g_old_masterkeys[i-1], 0x10, is_retail ? mkey_vectors[i] : mkey_vectors_dev[i], 0x10);
            set_aes_keyslot(KEYSLOT_SWITCH_TEMPKEY, g_old_masterkeys[i-1], 0x10);
            check_keyslot = KEYSLOT_SWITCH_TEMPKEY;
        }
    }

    se_aes_ecb_decrypt_block(check_keyslot, final_vector, 0x10, is_retail ? mkey_vectors[0] : mkey_vectors_dev[0], 0x10);
    for (unsigned int i = 0; i < 0x10; i++) {
        if (final_vector[i] != 0) {
            return false;
        }
    }
    return true;
}

int mkey_detect_revision(bool is_retail) {
    if (g_determined_mkey_revision) {
        generic_panic();
    }

    for (unsigned int rev = 0; rev < MASTERKEY_REVISION_MAX; rev++) {
        if (check_mkey_revision(rev, is_retail)) {
            g_determined_mkey_revision = true;
            g_mkey_revision = rev;
            break;
        }
    }

    /* We must have determined the master key, or we're not running on a Switch. */
    if (!g_determined_mkey_revision) {
        return -1;
    } else {
        return 0;
    }
}

unsigned int mkey_get_revision(void) {
    if (!g_determined_mkey_revision) {
        generic_panic();
    }

    return g_mkey_revision;
}

unsigned int mkey_get_keyslot(unsigned int revision) {
    if (!g_determined_mkey_revision || revision >= MASTERKEY_REVISION_MAX) {
        generic_panic();
    }

    if (revision > g_mkey_revision) {
        generic_panic();
    }

    if (revision == g_mkey_revision) {
        return (is_soc_mariko() ? KEYSLOT_SWITCH_MASTERKEY_MARIKO : KEYSLOT_SWITCH_MASTERKEY);
    } else {
        /* Load into a temp keyslot. */
        set_aes_keyslot(KEYSLOT_SWITCH_TEMPKEY, g_old_masterkeys[revision], 0x10);
        return KEYSLOT_SWITCH_TEMPKEY;
    }
}

void derive_new_device_keys(bool is_retail, unsigned int keygen_keyslot, unsigned int target_firmware) {
    const bool is_mariko = is_soc_mariko();

    uint8_t work_buffer[0x10];
    for (unsigned int revision = 0; revision < MASTERKEY_NUM_NEW_DEVICE_KEYS; revision++) {
        const unsigned int relative_revision = revision + MASTERKEY_REVISION_400_410;

        se_aes_ecb_decrypt_block(keygen_keyslot, work_buffer, 0x10, new_device_key_sources[revision], 0x10);
        decrypt_data_into_keyslot(KEYSLOT_SWITCH_TEMPKEY, mkey_get_keyslot(0), is_retail ? new_device_keygen_sources[revision] : new_device_keygen_sources_dev[revision], 0x10);
        if (relative_revision > mkey_get_revision()) {
            break;
        } else if (relative_revision == mkey_get_revision()) {
            /* On 7.0.0 erista, sept will have derived this key for us already. */
            if (target_firmware < ATMOSPHERE_TARGET_FIRMWARE_7_0_0 || is_mariko) {
                decrypt_data_into_keyslot(is_mariko ? KEYSLOT_SWITCH_DEVICEKEY_MARIKO : KEYSLOT_SWITCH_DEVICEKEY, KEYSLOT_SWITCH_TEMPKEY, work_buffer, 0x10);
            }
        } else {
            se_aes_ecb_decrypt_block(KEYSLOT_SWITCH_TEMPKEY, work_buffer, 0x10, work_buffer, 0x10);
            set_old_devkey(relative_revision, work_buffer);

            if (revision == 0 && is_mariko) {
                set_aes_keyslot(KEYSLOT_SWITCH_4XOLDDEVICEKEY, work_buffer, 0x10);
            }
        }
    }
}

void set_old_devkey(unsigned int revision, const uint8_t *key) {
    if (revision < MASTERKEY_REVISION_400_410 || MASTERKEY_REVISION_MAX <= revision) {
        generic_panic();
    }

    memcpy(g_old_devicekeys[revision - MASTERKEY_REVISION_400_410], key, 0x10);
}

unsigned int devkey_get_keyslot(unsigned int revision) {
    if (!g_determined_mkey_revision || revision > g_mkey_revision) {
        generic_panic();
    }

    if (revision < MASTERKEY_REVISION_400_410) {
        return KEYSLOT_SWITCH_4XOLDDEVICEKEY;
    } else if (revision < g_mkey_revision) {
        /* Load into a temp keyslot. */
        set_aes_keyslot(KEYSLOT_SWITCH_TEMPKEY, g_old_devicekeys[revision - MASTERKEY_REVISION_400_410], 0x10);
        return KEYSLOT_SWITCH_TEMPKEY;
    } else {
        return is_soc_mariko() ? KEYSLOT_SWITCH_DEVICEKEY_MARIKO : KEYSLOT_SWITCH_DEVICEKEY;
    }
}

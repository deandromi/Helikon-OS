/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The single canonical Helikon-OS error enum.
 *
 * `0` is success and every error is negative. Firmware modules, the HFS+ core
 * and the bootloader glue all use this vocabulary; there is no errno and no
 * second taxonomy. The historical `HFS_*` names are aliases so existing HFS+
 * code and tests stay source-compatible.
 */
#ifndef HELIKON_LIB_ERROR_H
#define HELIKON_LIB_ERROR_H

enum helikon_error {
    HELIKON_OK = 0,
    HELIKON_ERR_IO = -1,
    HELIKON_ERR_NOT_HFS = -2,
    HELIKON_ERR_FORMAT = -3,
    HELIKON_ERR_UNSUPPORTED = -4,
    HELIKON_ERR_DIRTY = -5,
    HELIKON_ERR_ARGUMENT = -6,
    HELIKON_ERR_BUFFER = -7,
    HELIKON_ERR_NOT_FOUND = -8,
    HELIKON_ERR_NOT_DIR = -9,
    HELIKON_ERR_IS_DIR = -10,
    HELIKON_ERR_NAME_INVALID = -11,
    HELIKON_ERR_NO_SPACE = -12,
    HELIKON_ERR_READ_ONLY = -13,
    HELIKON_ERR_BUSY = -14,
    HELIKON_ERR_POWER = -15,
    HELIKON_ERR_NOT_EMPTY = -16,
    /* Firmware addition (D-28): the loader handoff failed validation. */
    HELIKON_ERR_BOOT_INFO_INVALID = -17,
    /* Firmware addition (F2): bootstate is missing, invalid or mismatched. */
    HELIKON_ERR_BOOTSTATE = -18
};

/* HFS+ core compatibility names. */
#define HFS_OK HELIKON_OK
#define HFS_ERR_IO HELIKON_ERR_IO
#define HFS_ERR_NOT_HFS HELIKON_ERR_NOT_HFS
#define HFS_ERR_FORMAT HELIKON_ERR_FORMAT
#define HFS_ERR_UNSUPPORTED HELIKON_ERR_UNSUPPORTED
#define HFS_ERR_DIRTY HELIKON_ERR_DIRTY
#define HFS_ERR_ARGUMENT HELIKON_ERR_ARGUMENT
#define HFS_ERR_BUFFER HELIKON_ERR_BUFFER
#define HFS_ERR_NOT_FOUND HELIKON_ERR_NOT_FOUND
#define HFS_ERR_NOT_DIR HELIKON_ERR_NOT_DIR
#define HFS_ERR_IS_DIR HELIKON_ERR_IS_DIR
#define HFS_ERR_NAME_INVALID HELIKON_ERR_NAME_INVALID
#define HFS_ERR_NO_SPACE HELIKON_ERR_NO_SPACE
#define HFS_ERR_READ_ONLY HELIKON_ERR_READ_ONLY
#define HFS_ERR_BUSY HELIKON_ERR_BUSY
#define HFS_ERR_POWER HELIKON_ERR_POWER
#define HFS_ERR_NOT_EMPTY HELIKON_ERR_NOT_EMPTY
#define HFS_ERR_BOOT_INFO_INVALID HELIKON_ERR_BOOT_INFO_INVALID
#define HFS_ERR_BOOTSTATE HELIKON_ERR_BOOTSTATE

#endif

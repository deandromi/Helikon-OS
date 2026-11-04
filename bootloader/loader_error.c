/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "loader_error.h"

const char *helikon_loader_strerror(int error)
{
    switch (error) {
    case HELIKON_LOADER_OK: return "ok";
    case HELIKON_LOADER_ERR_IO: return "I/O error";
    case HELIKON_LOADER_ERR_ARGUMENT: return "invalid argument";
    case HELIKON_LOADER_ERR_BUFFER: return "buffer too small";
    case HELIKON_LOADER_ERR_NOT_FOUND: return "not found";
    case HELIKON_LOADER_ERR_MAGIC: return "bad manifest magic";
    case HELIKON_LOADER_ERR_VERSION: return "bad manifest version";
    case HELIKON_LOADER_ERR_BOARD: return "wrong board";
    case HELIKON_LOADER_ERR_IMAGE_VERSION: return "bad image version";
    case HELIKON_LOADER_ERR_ADDRESS: return "bad load address";
    case HELIKON_LOADER_ERR_SIZE: return "bad image size";
    case HELIKON_LOADER_ERR_HASH: return "image hash mismatch";
    case HELIKON_LOADER_ERR_SIGNATURE: return "signature rejected";
    case HELIKON_LOADER_ERR_FLAGS: return "unsupported manifest flags";
    case HELIKON_LOADER_ERR_BOOTSTATE: return "invalid boot state";
    case HELIKON_LOADER_ERR_NO_IMAGE: return "no bootable image";
    case HELIKON_LOADER_ERR_UNSUPPORTED: return "unsupported object";
    default: return "unknown loader error";
    }
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_unicode.h"

static int put_utf8(char *dest, size_t dest_size, size_t *used, uint32_t cp)
{
    if (cp < 0x80u) {
        if (*used + 1 >= dest_size)
            return HFS_ERR_BUFFER;
        dest[(*used)++] = (char)cp;
    } else if (cp < 0x800u) {
        if (*used + 2 >= dest_size)
            return HFS_ERR_BUFFER;
        dest[(*used)++] = (char)(0xc0u | (cp >> 6));
        dest[(*used)++] = (char)(0x80u | (cp & 0x3fu));
    } else if (cp < 0x10000u) {
        if (*used + 3 >= dest_size)
            return HFS_ERR_BUFFER;
        dest[(*used)++] = (char)(0xe0u | (cp >> 12));
        dest[(*used)++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
        dest[(*used)++] = (char)(0x80u | (cp & 0x3fu));
    } else {
        if (*used + 4 >= dest_size)
            return HFS_ERR_BUFFER;
        dest[(*used)++] = (char)(0xf0u | (cp >> 18));
        dest[(*used)++] = (char)(0x80u | ((cp >> 12) & 0x3fu));
        dest[(*used)++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
        dest[(*used)++] = (char)(0x80u | (cp & 0x3fu));
    }
    return HFS_OK;
}

int hfs_utf16be_to_utf8(const uint8_t *source, size_t units, char *dest,
                        size_t dest_size)
{
    if ((source == NULL && units != 0) || dest == NULL || dest_size == 0)
        return HFS_ERR_ARGUMENT;

    size_t used = 0;
    for (size_t i = 0; i < units; ++i) {
        uint32_t cp = ((uint32_t)source[i * 2] << 8) | source[i * 2 + 1];

        if (cp >= 0xd800u && cp <= 0xdbffu) {
            if (i + 1 >= units)
                return HFS_ERR_NAME_INVALID;
            uint32_t low = ((uint32_t)source[(i + 1) * 2] << 8) |
                           source[(i + 1) * 2 + 1];
            if (low < 0xdc00u || low > 0xdfffu)
                return HFS_ERR_NAME_INVALID;
            cp = 0x10000u + ((cp - 0xd800u) << 10) + (low - 0xdc00u);
            ++i;
        } else if (cp >= 0xdc00u && cp <= 0xdfffu) {
            return HFS_ERR_NAME_INVALID;
        }

        if (cp == 0)
            return HFS_ERR_NAME_INVALID;
        int rc = put_utf8(dest, dest_size, &used, cp);
        if (rc != HFS_OK)
            return rc;
    }

    dest[used] = '\0';
    return HFS_OK;
}

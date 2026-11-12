#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hfs_btree.h"
#include "hfs_volume.h"

static uint8_t node[8192];

static int node_read(void *context, uint64_t offset, void *buffer, size_t length)
{
    (void)context;
    if (offset > sizeof(node) || length > sizeof(node) - offset)
        return -1;
    memcpy(buffer, node + offset, length);
    return 0;
}

int main(void)
{
    static const uint8_t prefix[0x38] = {
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
        0x01,0x00,0x00,0x03, 0x00,0x00,0x00,0x02,
        0x00,0x00,0x00,0x03, 0x00,0x00,0x01,0x00,
        0x00,0x00,0x00,0x07, 0x00,0x00,0x00,0x01,
        0x20,0x00,0x02,0x04, 0x00,0x00,0x25,0x00,
        0x00,0x00,0x24,0xf6, 0x00,0x00,0x04,0xa0,
        0x00,0x00,0x00,0xcf, 0x00,0x00,0x00,0x06
    };
    memcpy(node, prefix, sizeof(prefix));
    node[0x1ff8] = 0x1f; node[0x1ff9] = 0xf8;
    node[0x1ffa] = 0x00; node[0x1ffb] = 0xf8;
    node[0x1ffc] = 0x00; node[0x1ffd] = 0x78;
    node[0x1ffe] = 0x00; node[0x1fff] = 0x0e;

    struct hfs_volume volume;
    memset(&volume, 0, sizeof(volume));
    volume.read = node_read;
    volume.base = 0;
    volume.length = 127663603712ull;
    volume.block_size = 4096;
    volume.blocks = 31167872;
    volume.catalog.size = 77594624ull;
    volume.catalog.blocks = 18944;
    volume.catalog.extents[0].start = 0;
    volume.catalog.extents[0].count = 18944;

    struct hfs_btree btree;
    uint8_t scratch[32768];
    int rc = hfs_btree_open(&btree, &volume, &volume.catalog, scratch,
                            sizeof(scratch));
    printf("hfs_btree_open rc=%d depth=%u root=%u node_size=%u total=%u first=%u last=%u leafrec=%u\n",
           rc, btree.depth, btree.root_node, btree.node_size,
           btree.total_nodes, btree.first_leaf, btree.last_leaf,
           btree.leaf_records);
    return rc == HFS_OK ? 0 : 1;
}

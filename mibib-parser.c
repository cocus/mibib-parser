#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <endian.h>
#include <sys/cdefs.h>

#include <inttypes.h> // PRIu64

/* all of this comes from https://elixir.bootlin.com/linux/latest/source/drivers/mtd/parsers/qcomsmempart.c */

#define SMEM_AARM_PARTITION_TABLE    9
#define SMEM_APPS            0

#define SMEM_FLASH_PART_MAGIC1        0x55ee73aa
#define SMEM_FLASH_PART_MAGIC2        0xe35ebddb
#define SMEM_FLASH_PTABLE_V3        3
#define SMEM_FLASH_PTABLE_V4        4
#define SMEM_FLASH_PTABLE_MAX_PARTS_V3    16
#define SMEM_FLASH_PTABLE_MAX_PARTS_V4    48
#define SMEM_FLASH_PTABLE_HDR_LEN    (4 * sizeof(uint32_t))
#define SMEM_FLASH_PTABLE_NAME_SIZE    16

/**
 * struct smem_flash_pentry - SMEM Flash partition entry
 * @name: Name of the partition
 * @offset: Offset in blocks
 * @length: Length of the partition in blocks
 * @attr: Flags for this partition
 */
struct smem_flash_pentry {
    char name[SMEM_FLASH_PTABLE_NAME_SIZE];
    uint32_t offset;
    uint32_t length;
    uint8_t attr;
} __attribute__ ((aligned(4)));

/**
 * struct smem_flash_ptable - SMEM Flash partition table
 * @magic1: Partition table Magic 1
 * @magic2: Partition table Magic 2
 * @version: Partition table version
 * @numparts: Number of partitions in this ptable
 * @pentry: Flash partition entries belonging to this ptable
 */
struct smem_flash_ptable {
    uint32_t magic1;
    uint32_t magic2;
    uint32_t version;
    uint32_t numparts;
    struct smem_flash_pentry pentry[SMEM_FLASH_PTABLE_MAX_PARTS_V4];
} __attribute__ ((aligned(4)));


static void humanSize(uint64_t bytes, char *buffer)
{
	char *suffix[] = {"B", "KB", "MB", "GB", "TB"};
	char length = sizeof(suffix) / sizeof(suffix[0]);

	int i = 0;
	double dblBytes = bytes;

	if (bytes >= 1024) {
		for (i = 0; (bytes / 1024) > 0 && i<length-1; i++, bytes /= 1024)
			dblBytes = bytes / 1024.0;
	}

	sprintf(buffer, "%.02lf %s", dblBytes, suffix[i]);
}

int main(int argc, char **argv)
{
    int ret = 0;

    FILE* fd = NULL;

    const char filename[] = "partition_complete_p2K_b128K.mbn";//"BG95M3LAR02A04_01.001.01.001_partition_complete_p2K_b128K.mbn";

    const unsigned long block_size = 1024*128;

    printf("Filename: '%s'\n", filename);
    {
        char string_blk_size[200];
        humanSize(block_size, string_blk_size);
        printf("Block Size: %s\n", string_blk_size);
    }

    fd = fopen(filename, "rb");

    if (fd < 0)
    {
        printf("Can't open file '%s'!\n", filename);
        return 1;
    }

    fseek(fd, 0, SEEK_END);
    long fsize = ftell(fd);
    fseek(fd, 0, SEEK_SET);

    char *the_file = NULL;
    the_file = (char*)malloc(fsize);
    if (!the_file)
    {
        ret = 2;
        printf("Not enough memory to allocate %ld bytes\n", fsize);
        goto cleanup1;
    }

    size_t rd = fread(the_file, 1, fsize, fd);
    if (rd != fsize)
    {
        ret = 3;
        printf("couldn't read %ld bytes\n", fsize);
        goto cleanup2;
    }

    struct smem_flash_ptable *ptable = (struct smem_flash_ptable*)(the_file+0x800);

    /* Verify ptable magic */
    if (le32toh(ptable->magic1) != SMEM_FLASH_PART_MAGIC1 ||
        le32toh(ptable->magic2) != SMEM_FLASH_PART_MAGIC2) {
        printf("Partition table magic verification failed\n");
        ret = 4;
        goto cleanup2;
    }

    int numparts;

    /* Ensure that # of partitions is less than the max we have allocated */
    numparts = le32toh(ptable->numparts);
    if (numparts > SMEM_FLASH_PTABLE_MAX_PARTS_V4) {
        printf("Partition numbers exceed the max limit\n");
        ret = 5;
        goto cleanup2;
    }

    size_t len = SMEM_FLASH_PTABLE_HDR_LEN;

    /* Find out length of partition data based on table version */
    if (le32toh(ptable->version) <= SMEM_FLASH_PTABLE_V3) {
        len = SMEM_FLASH_PTABLE_HDR_LEN + SMEM_FLASH_PTABLE_MAX_PARTS_V3 *
            sizeof(struct smem_flash_pentry);
    } else if (le32toh(ptable->version) == SMEM_FLASH_PTABLE_V4) {
        len = SMEM_FLASH_PTABLE_HDR_LEN + SMEM_FLASH_PTABLE_MAX_PARTS_V4 *
            sizeof(struct smem_flash_pentry);
    } else {
        printf("Unknown ptable version (%d)", le32toh(ptable->version));
        ret = 6;
        goto cleanup2;
    }

    printf("MIBIB partition table found: ver: %d len: %d\n",
         le32toh(ptable->version), numparts);

    struct smem_flash_pentry *pentry;
    int i;
    char *name;

    for (i = 0; i < numparts; i++) {
        pentry = &ptable->pentry[i];
        if (pentry->name[0] == '\0')
            continue;

        char string_offset[200];
        char string_size[200];

        humanSize(le32toh(pentry->offset) * block_size, string_offset);
        humanSize(le32toh(pentry->length) * block_size, string_size);
//        printf("Partition entry %d: '%s', offset=0x%08x, size=0x%08x, attr=0x%08x\n",
        printf("part[%d]: '%s', offset=%s, size=%s, attr=0x%02x\n",
            i, pentry->name, string_offset, string_size, pentry->attr);
    }


cleanup2:
    free(the_file);

cleanup1:
    fclose(fd);    

    return ret;
}
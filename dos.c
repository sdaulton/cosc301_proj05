#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"


static int imagesize = 0;

/* memory map the FAT-12  disk image file */
uint8_t *mmap_file(char *filename, int *fd)
{
    struct stat statbuf;
    uint8_t *image_buf;
    char pathname[MAXPATHLEN+1];


    /* If filename isn't an absolute pathname, then we'd better prepend
       the current working directory to it */

    if (filename[0] == '/') 
    {
	strncpy(pathname, filename, MAXPATHLEN);
    } 
    else 
    {
	getcwd(pathname, MAXPATHLEN);
	if (strlen(pathname) + strlen(filename) + 1 > MAXPATHLEN) 
	{
	    fprintf(stderr, "Filename too long\n");
	    exit(1);
	}
	strcat(pathname, "/");
	strcat(pathname, filename);
    }


    /* Step 2: find out how big the disk image file is */
    /* we can use "stat" to do this, by checking the file status */

    if (stat(pathname, &statbuf) < 0) 
    {
	fprintf(stderr, "Cannot read disk image file %s:\n%s\n", 
		pathname, strerror(errno));
	exit(1);
    }
    imagesize = statbuf.st_size;


    /* Step 3: open the file for read/write */

    *fd = open(pathname, O_RDWR);
    if (*fd < 0) 
    {
	fprintf(stderr, "Cannot read disk image file %s:\n%s\n", 
		pathname, strerror(errno));
	exit(1);
    }


    /* Step 4: we memory map the file */

    image_buf = mmap(NULL, imagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE, *fd, 0);
    if (image_buf == MAP_FAILED) 
    {
	fprintf(stderr, "Failed to memory map: \n%s\n", strerror(errno));
	exit(1);
    }
    return image_buf;
}


void unmmap_file(uint8_t *image, int *fd)
{
    msync(image, imagesize, MS_SYNC);
    munmap(image, imagesize);
    close(*fd);
}


/* read the bootsector from the disk, and check that it is sane */
/* define DEBUG to see what the disk parameters actually are */

struct bpb33* check_bootsector(uint8_t *image_buf)
{
    struct bootsector33* bootsect;
    struct byte_bpb33* bpb;  /* BIOS parameter block */
    struct bpb33* bpb_aligned;

#ifdef DEBUG
    fprintf(stderr, "Size of BPB: %lu\n", sizeof(struct bootsector33));
#endif

    bootsect = (struct bootsector33*)image_buf;
    if (bootsect->bsJump[0] == 0xe9 ||
	(bootsect->bsJump[0] == 0xeb && bootsect->bsJump[2] == 0x90)) 
    {
#ifdef DEBUG
	fprintf(stderr, "Found good jump instruction in boot sector\n");
#endif
    } 
    else 
    {
	fprintf(stderr, "illegal boot sector jump inst: %x%x%x\n", 
		bootsect->bsJump[0], bootsect->bsJump[1], 
		bootsect->bsJump[2]); 
    } 

#ifdef DEBUG
    fprintf(stderr, "OemName: %s\n", bootsect->bsOemName);
#endif

    if (bootsect->bsBootSectSig0 == BOOTSIG0
	&& bootsect->bsBootSectSig0 == BOOTSIG0) 
    {
	//Good boot sector sig;
#ifdef DEBUG
	fprintf(stderr, "Good boot sector signature\n");
#endif
    } 
    else 
    {
	fprintf(stderr, "Boot sector signature %x%x\n", 
		bootsect->bsBootSectSig0, 
		bootsect->bsBootSectSig1);
    }

    bpb = (struct byte_bpb33*)&(bootsect->bsBPB[0]);

    /* bpb is a byte-based struct, because this data is unaligned.
       This makes it hard to access the multi-byte fields, so we copy
       it to a slightly larger struct that is word-aligned */
    bpb_aligned = malloc(sizeof(struct bpb33));

    bpb_aligned->bpbBytesPerSec = getushort(bpb->bpbBytesPerSec);
    bpb_aligned->bpbSecPerClust = bpb->bpbSecPerClust;
    bpb_aligned->bpbResSectors = getushort(bpb->bpbResSectors);
    bpb_aligned->bpbFATs = bpb->bpbFATs;
    bpb_aligned->bpbRootDirEnts = getushort(bpb->bpbRootDirEnts);
    bpb_aligned->bpbSectors = getushort(bpb->bpbSectors);
    bpb_aligned->bpbFATsecs = getushort(bpb->bpbFATsecs);
    bpb_aligned->bpbHiddenSecs = getushort(bpb->bpbHiddenSecs);
    

#ifdef DEBUG
    fprintf(stderr, "Bytes per sector: %d\n", bpb_aligned->bpbBytesPerSec);
    fprintf(stderr, "Sectors per cluster: %d\n", bpb_aligned->bpbSecPerClust);
    fprintf(stderr, "Reserved sectors: %d\n", bpb_aligned->bpbResSectors);
    fprintf(stderr, "Number of FATs: %d\n", bpb->bpbFATs);
    fprintf(stderr, "Number of root dir entries: %d\n", bpb_aligned->bpbRootDirEnts);
    fprintf(stderr, "Total number of sectors: %d\n", bpb_aligned->bpbSectors);
    fprintf(stderr, "Number of sectors per FAT: %d\n", bpb_aligned->bpbFATsecs);
    fprintf(stderr, "Number of hidden sectors: %d\n", bpb_aligned->bpbHiddenSecs);
#endif

    return bpb_aligned;
}

/* get_fat_entry returns the value from the FAT entry for
   clusternum. */
uint16_t get_fat_entry(uint16_t clusternum, 
		       uint8_t *image_buf, struct bpb33* bpb)
{
    uint32_t offset;
    uint16_t value;
    uint8_t b1, b2;
    
    /* this involves some really ugly bit shifting.  This probably
       only works on a little-endian machine. */
    offset = bpb->bpbResSectors * bpb->bpbBytesPerSec * bpb->bpbSecPerClust 
	+ (3 * (clusternum/2));
    switch(clusternum % 2) 
    {
    case 0:
	b1 = *(image_buf + offset);
	b2 = *(image_buf + offset + 1);

	/* mjh: little-endian CPUs are ugly! */
	value = ((0x0f & b2) << 8) | b1;
	break;
    case 1:
	b1 = *(image_buf + offset + 1);
	b2 = *(image_buf + offset + 2);
	value = b2 << 4 | ((0xf0 & b1) >> 4);
	break;
    }
    return value;
}


/* set_fat_entry sets the value of the FAT entry for clusternum to value. */
void set_fat_entry(uint16_t clusternum, uint16_t value,
		   uint8_t *image_buf, struct bpb33* bpb)
{
    uint32_t offset;
    uint8_t *p1, *p2;
    
    /* this involves some really ugly bit shifting.  This probably
       only works on a little-endian machine. */
    offset = bpb->bpbResSectors * bpb->bpbBytesPerSec * bpb->bpbSecPerClust 
	+ (3 * (clusternum/2));
    switch(clusternum % 2) 
    {
    case 0:
	p1 = image_buf + offset;
	p2 = image_buf + offset + 1;
	/* mjh: little-endian CPUs are really ugly! */
	*p1 = (uint8_t)(0xff & value);
	*p2 = (uint8_t)((0xf0 & (*p2)) | (0x0f & (value >> 8)));
	break;
    case 1:
	p1 = image_buf + offset + 1;
	p2 = image_buf + offset + 2;
	*p1 = (uint8_t)((0x0f & (*p1)) | ((0x0f & value) << 4));
	*p2 = (uint8_t)(0xff & (value >> 4));
	break;
    }
}


int is_valid_cluster(uint16_t cluster, struct bpb33 *bpb)
{
    uint16_t max_cluster = (bpb->bpbSectors / bpb->bpbSecPerClust) & FAT12_MASK;

    if (cluster >= (FAT12_MASK & CLUST_FIRST) && 
        cluster <= (FAT12_MASK & CLUST_LAST) &&
        cluster < max_cluster)
        return TRUE;
    return FALSE;
}


/* is_end_of_file returns true if the FAT entry for cluster indicates
   this is the last cluster in a file */
int is_end_of_file(uint16_t cluster) 
{
    if (cluster >= (FAT12_MASK & CLUST_EOFS) && 
        cluster <= (FAT12_MASK & CLUST_EOFE)) 
    {
	return TRUE;
    } 
    else 
    {
	return FALSE;
    }
}


/* root_dir_addr returns the address in the mmapped disk image for the
   start of the root directory, as indicated in the boot sector */
uint8_t *root_dir_addr(uint8_t *image_buf, struct bpb33* bpb)
{
    uint32_t offset;
    offset = 
	(bpb->bpbBytesPerSec 
	 * (bpb->bpbResSectors + (bpb->bpbFATs * bpb->bpbFATsecs)));
    return image_buf + offset;
}


/* cluster_to_addr returns the memory location where the memory mapped
   cluster actually starts */
uint8_t *cluster_to_addr(uint16_t cluster, uint8_t *image_buf, 
			 struct bpb33* bpb)
{
    uint8_t *p;
    p = root_dir_addr(image_buf, bpb);
    if (cluster != MSDOSFSROOT) 
    {
	/* move to the end of the root directory */
	p += bpb->bpbRootDirEnts * sizeof(struct direntry);

	/* move forward the right number of clusters */
	p += bpb->bpbBytesPerSec * bpb->bpbSecPerClust * (cluster - CLUST_FIRST);
    }
    return p;
}


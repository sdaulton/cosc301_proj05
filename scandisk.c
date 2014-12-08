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

void usage(char *progname) {
    fprintf(stderr, "usage: %s <imagename>\n", progname);
    exit(1);
}



void print_indent(int indent)
{
    int i;
    for (i = 0; i < indent*4; i++)
        printf(" ");
}

/*
 * Modified from dos_ls.c
 * returns the
 */

uint16_t print_dirent(struct direntry *dirent, int indent)
{
    uint16_t followclust = 0;
    
    int i;
    char name[9];
    char extension[4];
    uint32_t size;
    uint16_t file_cluster;
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    if (name[0] == SLOT_EMPTY)
    {
        return followclust;
    }
    
    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED)
    {
        return followclust;
    }
    
    if (((uint8_t)name[0]) == 0x2E)
    {
        // dot entry ("." or "..")
        // skip it
        return followclust;
    }
    
    /* names are space padded - remove the spaces */
    for (i = 8; i > 0; i--)
    {
        if (name[i] == ' ')
            name[i] = '\0';
        else
            break;
    }
    
    /* remove the spaces from extensions */
    for (i = 3; i > 0; i--)
    {
        if (extension[i] == ' ')
            extension[i] = '\0';
        else
            break;
    }
    
    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN)
    {
        // ignore any long file name extension entries
        //
        // printf("Win95 long-filename entry seq 0x%0x\n", dirent->deName[0]);
    }
    else if ((dirent->deAttributes & ATTR_VOLUME) != 0)
    {
        printf("Volume: %s\n", name);
    }
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0)
    {
        // don't deal with hidden directories; MacOS makes these
        // for trash directories and such; just ignore them.
        if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN)
        {
            print_indent(indent);
            printf("%s/ (directory)\n", name);
            file_cluster = getushort(dirent->deStartCluster);
            followclust = file_cluster;
        }
    }
    else
    {
        /*
         * a "regular" file entry
         * print attributes, size, starting cluster, etc.
         */
        int ro = (dirent->deAttributes & ATTR_READONLY) == ATTR_READONLY;
        int hidden = (dirent->deAttributes & ATTR_HIDDEN) == ATTR_HIDDEN;
        int sys = (dirent->deAttributes & ATTR_SYSTEM) == ATTR_SYSTEM;
        int arch = (dirent->deAttributes & ATTR_ARCHIVE) == ATTR_ARCHIVE;
        
        size = getulong(dirent->deFileSize);
        print_indent(indent);
        printf("%s.%s (%u bytes) (starting cluster %d) %c%c%c%c\n",
               name, extension, size, getushort(dirent->deStartCluster),
               ro?'r':' ',
               hidden?'h':' ',
               sys?'s':' ',
               arch?'a':' ');
    }
    
    return followclust;
}

// returns 1 if it is a normal file, 0 if not
uint16_t is_file(struct direntry *dirent, int indent)
{
    uint16_t isNormalFile = 0;
    
    int i;
    char name[9];
    char extension[4];
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    if (name[0] == SLOT_EMPTY)
    {
        return isNormalFile;
    }
    
    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED)
    {
        return isNormalFile;
    }
    
    if (((uint8_t)name[0]) == 0x2E)
    {
        // dot entry ("." or "..")
        // skip it
        return isNormalFile;
    }
    
    /* names are space padded - remove the spaces */
    for (i = 8; i > 0; i--)
    {
        if (name[i] == ' ')
            name[i] = '\0';
        else
            break;
    }
    
    /* remove the spaces from extensions */
    for (i = 3; i > 0; i--)
    {
        if (extension[i] == ' ')
            extension[i] = '\0';
        else
            break;
    }
    
    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN)
    {
        // ignore any long file name extension entries
        //
        // printf("Win95 long-filename entry seq 0x%0x\n", dirent->deName[0]);
    }
    else if ((dirent->deAttributes & ATTR_VOLUME) != 0)
    {
        // do nothing
    }
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0)
    {
        //do nothing
    }
    else
    {
        /*
         * a "regular" file entry
         *
         */
        
        isNormalFile = 1;
    }
    
    return isNormalFile;
}

// Takes the start cluster number as a parameter and returns the length of the cluster chain (i.e. number of clusters in file)
int get_chain_length(uint16_t startCluster, uint8_t *image_buf, struct bpb33* bpb) {
    int numClusters = 1;
    uint16_t nextCluster = get_fat_entry(startCluster, image_buf, bpb);
    while (!is_end_of_file(nextCluster)) {
        nextCluster = get_fat_entry(nextCluster, image_buf, bpb);
        numClusters++;
    }
    return numClusters;
}

// fixes the situation where a FAT chain is shorter than the expected filesize
void dir_entry_fixer(struct direntry *dirent, int chainLength) {
    uint32_t size;
    // size = math to get correct size from the chain length?? 
    putulong(dirent->deFileSize)
}

// fixes the situation where a FAT chain is longer than the correct file size
void fat_chain_fixer(uint16_t startCluster, uint8_t *image_buf, struct bpb33* bpb, uint32_t expectedChainLength) {
    int currentNum = 1;
    uint16_t nextCluster = get_fat_entry(startCluster, image_buf, bpb);

    // cycle through the chain until the stopping point
    while (currentNum < expectedChainLength) {
        nextCluster = get_fat_entry(nextCluster, image_buf, bpb);
        printf("nextCluster: %d\n", nextCluster);
        currentNum++;
    }

    // set the new last cluster to EOF
    printf("pre-fix nextCluster: %d\n", nextCluster);
    set_fat_entry(nextCluster, (FAT12_MASK & CLUST_EOFS), image_buf, bpb);
    printf("post-fix nextCluster: %d\n", nextCluster);

    // free any clusters past the correct size
    while (!is_end_of_file(nextCluster)) {
        uint16_t toFree = nextCluster;
        nextCluster = get_fat_entry(nextCluster, image_buf, bpb);
        printf("freed nextCluster: %d\n", nextCluster);
        set_fat_entry(toFree, CLUST_FREE, image_buf, bpb);
    }

    // frees the old EOF
    set_fat_entry(nextCluster, CLUST_FREE, image_buf, bpb);
    printf("last nextCluster: %d\n", nextCluster);

}

void follow_dir(uint16_t cluster, int indent,
                uint8_t *image_buf, struct bpb33* bpb)
{
    uint32_t size = 0;
    uint16_t startCluster = 0;
    uint32_t expectedChainLength = 0;
    int chainLength = 0;
    while (is_valid_cluster(cluster, bpb))
    {
        struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
        
        int numDirEntries = (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) / sizeof(struct direntry);
        int i = 0;
        for ( ; i < numDirEntries; i++)
        {
            // for each file in this directory
            uint16_t followclust = print_dirent(dirent, indent);
            if (is_file(dirent, indent)) {
                // dirent is for a regular file
                
                size = getulong(dirent->deFileSize);
                startCluster = getushort(dirent->deStartCluster);
                // check that length of cluster chain == size
                chainLength = get_chain_length(startCluster, image_buf, bpb);
                // ceiling division
                expectedChainLength = (size % 512) ? (size / 512 + 1) : (size / 512);
                if (chainLength != expectedChainLength) {
                    printf("INCONSISTENCY: expected chain length (%u clusters) does not match length of cluster chain (%d clusters)\n", expectedChainLength, chainLength);
                    if (chainLength > expectedChainLength) {
                        printf("Too long!\n");
                        fat_chain_fixer(startCluster, image_buf, bpb, expectedChainLength);
                    }
                    else {
                        printf("Too short!\n");
                        dir_entry_fixer(dirent, chainLength);
                    }
                    printf("Now it's fixed!\n");
                } else {
                    //printf("expected chain length (%u clusters) matches length of cluster chain (%d clusters)\n", expectedChainLength, chainLength);
                }
            }
            if (followclust) {
                // dirent is for a directory
                follow_dir(followclust, indent+1, image_buf, bpb);
            }
            dirent++;
        }
        
        cluster = get_fat_entry(cluster, image_buf, bpb);
    }
}


void traverse_root(uint8_t *image_buf, struct bpb33* bpb)
{
    uint16_t cluster = 0;
    
    struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
    
    int i = 0;
    for ( ; i < bpb->bpbRootDirEnts; i++)
    {
        uint16_t followclust = print_dirent(dirent, 0);
        if (is_valid_cluster(followclust, bpb)) {
            follow_dir(followclust, 1, image_buf, bpb);
        }
        dirent++;
    }
}


int main(int argc, char** argv) {
    uint8_t *image_buf;
    int fd;
    struct bpb33* bpb;
    if (argc < 2) {
	usage(argv[0]);
    }

    image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf);
    traverse_root(image_buf, bpb);
    // your code should start here...





    unmmap_file(image_buf, &fd);
    return 0;
}

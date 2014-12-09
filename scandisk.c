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
#include "refc.c"

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

// returns an integer representing the cluster type, used in the cluster references data strucutre
int get_cluster_type(uint16_t clusterNum, uint8_t *image_buf, struct bpb33* bpb) {
    uint16_t fatEntry = get_fat_entry(clusterNum, image_buf, bpb);
    if (is_valid_cluster(fatEntry, bpb)) {
        return 1;
    } else if (is_end_of_file(fatEntry)) {
        return 2;
    } else if (fatEntry == (FAT12_MASK & CLUST_BAD)) {
        return 3;
    } else if (fatEntry >= (FAT12_MASK & CLUST_RSRVDS) &&
                             fatEntry <= (FAT12_MASK & CLUST_RSRVDE)) {
        return 4;
    } else if (fatEntry == (FAT12_MASK & CLUST_FREE)) {
        return 0;
    } else {
        printf("Error setting cluster type for cluster number %d\n", clusterNum);
        return -1;
    }
}

// Takes the start cluster number as a parameter and returns the length of the cluster chain (i.e. number of clusters in file)
int get_chain_length(uint16_t startCluster, uint8_t *image_buf, struct bpb33* bpb, struct node *references[]) {
    int numClusters = 1;
    uint16_t nextCluster = get_fat_entry(startCluster, image_buf, bpb);
    while (!is_end_of_file(nextCluster)) {
        references[nextCluster]->inDir = 1;
        references[nextCluster]->type = get_cluster_type(nextCluster, image_buf, bpb);
        nextCluster = get_fat_entry(nextCluster, image_buf, bpb);
        numClusters++;
    }
    return numClusters;
}

// fixes the situation where a FAT chain is shorter than the expected filesize
void dir_entry_fixer(struct direntry *dirent, int chainLength) {
    uint32_t size = chainLength * 512;
    putulong(dirent->deFileSize, size);
}

// fixes the situation where a FAT chain is longer than the correct file size
void fat_chain_fixer(uint16_t startCluster, uint8_t *image_buf, struct bpb33* bpb, uint32_t expectedChainLength, struct node *references[]) {
    int currentNum = 1;
    printf("startCluster: %d\n", startCluster);
    uint16_t prevCluster = startCluster;
    uint16_t nextCluster = get_fat_entry(startCluster, image_buf, bpb);
    //currentNum++; // SAM ADDED THIS
    
    // cycle through the chain until the stopping point
    while (currentNum < expectedChainLength) {
        prevCluster = nextCluster;
        nextCluster = get_fat_entry(nextCluster, image_buf, bpb);
        printf("nextCluster: %d\n", nextCluster);
        currentNum++;
    }

    
    // free any clusters past the correct size
    //nextCluster = get_fat_entry(nextCluster, image_buf, bpb);
    while (!is_end_of_file(get_fat_entry(nextCluster, image_buf, bpb))) {
        uint16_t toFree = nextCluster;
        //update references
        references[nextCluster]->inDir = 0;
        references[nextCluster]->type = 0;
        nextCluster = get_fat_entry(nextCluster, image_buf, bpb);
        printf("freed nextCluster: %d\n", nextCluster);
        set_fat_entry(toFree, CLUST_FREE, image_buf, bpb);
    }

    // frees the old EOF
    references[nextCluster]->inDir = 0;
    references[nextCluster]->type = 0;
    set_fat_entry(nextCluster, CLUST_FREE, image_buf, bpb);
    printf("last nextCluster: %d\n", nextCluster);
    
    // set the new last cluster to EOF
    printf("pre-fix nextCluster: %d\n", prevCluster);
    set_fat_entry(prevCluster, (FAT12_MASK & CLUST_EOFS), image_buf, bpb);
    //update references
    references[prevCluster]->type = 2;
    prevCluster = get_fat_entry(prevCluster, image_buf, bpb);
    printf("post-fix nextCluster: %d\n", prevCluster);

}

void follow_dir(uint16_t cluster, int indent,
                uint8_t *image_buf, struct bpb33* bpb, struct node *references[])
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
            printf("3 Here\n");
            if (is_file(dirent, indent)) {
                // dirent is for a regular file
                printf("4 Here\n");
                size = getulong(dirent->deFileSize);
                startCluster = getushort(dirent->deStartCluster);
                // update cluster references
                references[startCluster]->inDir = 1;
                references[startCluster]->type = get_cluster_type(startCluster, image_buf, bpb);
                // check that length of cluster chain == size
                chainLength = get_chain_length(startCluster, image_buf, bpb, references);
                // ceiling division
                expectedChainLength = (size % 512) ? (size / 512 + 1) : (size / 512);
                if (chainLength != expectedChainLength) {
                    printf("INCONSISTENCY: expected chain length (%u clusters) does not match length of cluster chain (%d clusters)\n", expectedChainLength, chainLength);
                    if (chainLength > expectedChainLength) {
                        fat_chain_fixer(startCluster, image_buf, bpb, expectedChainLength, references);
                    }
                    else {
                        dir_entry_fixer(dirent, chainLength);
                    }
                } else {
                    //printf("expected chain length (%u clusters) matches length of cluster chain (%d clusters)\n", expectedChainLength, chainLength);
                }
            }
            if (followclust) {
                printf("5 Here\n");
                // dirent is for a directory
                follow_dir(followclust, indent+1, image_buf, bpb, references);
            }
            dirent++;
            printf("6 Here\n");
        }
        
        cluster = get_fat_entry(cluster, image_buf, bpb);
    }
}


void traverse_root(uint8_t *image_buf, struct bpb33* bpb, struct node *references[])
{
    uint16_t cluster = 0;
    
    struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
    
    int i = 0;
    for ( ; i < bpb->bpbRootDirEnts; i++)
    {
        printf("1 Here\n");
        uint16_t followclust = print_dirent(dirent, 0);
        if (is_valid_cluster(followclust, bpb)) {
            printf("2 Here\n");
            
            follow_dir(followclust, 1, image_buf, bpb, references);
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
    int numDataClusters = bpb->bpbSectors - 1 - 9 - 9 - 14;
    // initialize data structure to store information about each cluster
    struct node *references[numDataClusters + 2]; // only + 2 to create idempotent mapping from cluster number to index
    for (int i = 2; i < numDataClusters; i ++) {
        references[i] = malloc(sizeof(struct node));
        node_init(references[i]);
    }
    // traverse directory entries to gather metadata
    traverse_root(image_buf, bpb, references);
    
    //NEXT STEP
    // now traverse FAT to look for orphan blocks
    // for each fat entry that is not empty (CHECK THIS STRAIGHT OUT OF THE FAT)
    // but the cluster type is not set in the references data structure
    // create a new direntry in the root directory for that orphan block (and string connected orphans together if there is a cluster chain) in the FAT
    



    unmmap_file(image_buf, &fd);
    for (int i = 2; i < 40; i++) {
        printf("Cluster Number: %d; inFat: %d; inDir: %d; type: %d\n", i, references[i]->inFat,references[i]->inDir, references[i]->type);
        free(references[i]);
    }
    // remember to FREE references
    return 0;
}

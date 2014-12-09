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

/* write the values into a directory entry -- taken from dos_cp.c */
void write_dirent(struct direntry *dirent, char *filename, 
          uint16_t start_cluster, uint32_t size)
{
    char *p, *p2;
    char *uppername;
    int len, i;

    /* clean out anything old that used to be here */
    memset(dirent, 0, sizeof(struct direntry));

    /* extract just the filename part */
    uppername = strdup(filename);
    p2 = uppername;
    for (i = 0; i < strlen(filename); i++) 
    {
    if (p2[i] == '/' || p2[i] == '\\') 
    {
        uppername = p2+i+1;
    }
    }

    /* convert filename to upper case */
    for (i = 0; i < strlen(uppername); i++) 
    {
    uppername[i] = toupper(uppername[i]);
    }

    /* set the file name and extension */
    memset(dirent->deName, ' ', 8);
    p = strchr(uppername, '.');
    memcpy(dirent->deExtension, "___", 3);
    if (p == NULL) 
    {
    fprintf(stderr, "No filename extension given - defaulting to .___\n");
    }
    else 
    {
    *p = '\0';
    p++;
    len = strlen(p);
    if (len > 3) len = 3;
    memcpy(dirent->deExtension, p, len);
    }

    if (strlen(uppername)>8) 
    {
    uppername[8]='\0';
    }
    memcpy(dirent->deName, uppername, strlen(uppername));
    free(p2);

    /* set the attributes and file size */
    dirent->deAttributes = ATTR_NORMAL;
    putushort(dirent->deStartCluster, start_cluster);
    putulong(dirent->deFileSize, size);

    /* could also set time and date here if we really
       cared... */
}


/* create_dirent finds a free slot in the directory, and write the
   directory entry -- taken from dos_cp.c*/

void create_dirent(struct direntry *dirent, char *filename, 
           uint16_t start_cluster, uint32_t size,
           uint8_t *image_buf, struct bpb33* bpb)
{
    while (1) 
    {
    if (dirent->deName[0] == SLOT_EMPTY) 
    {
        /* we found an empty slot at the end of the directory */
        write_dirent(dirent, filename, start_cluster, size);
        dirent++;

        /* make sure the next dirent is set to be empty, just in
           case it wasn't before */
        memset((uint8_t*)dirent, 0, sizeof(struct direntry));
        dirent->deName[0] = SLOT_EMPTY;
        return;
    }

    if (dirent->deName[0] == SLOT_DELETED) 
    {
        /* we found a deleted entry - we can just overwrite it */
        write_dirent(dirent, filename, start_cluster, size);
        return;
    }
    dirent++;
    }
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
    if (fatEntry >= (FAT12_MASK & CLUST_FIRST) && fatEntry <= (FAT12_MASK & CLUST_LAST)) {
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
    uint16_t eofCluster = get_fat_entry(startCluster, image_buf, bpb);
    if (is_end_of_file(eofCluster)) {
        //startCluster contains EOF
        return numClusters;
    }
    uint16_t nextCluster = get_fat_entry(eofCluster, image_buf, bpb);

    while (!is_end_of_file(nextCluster)) {
        references[eofCluster]->inDir = 1;
        references[eofCluster]->type = get_cluster_type(eofCluster, image_buf, bpb);
        eofCluster = nextCluster;
        nextCluster = get_fat_entry(nextCluster, image_buf, bpb);
        numClusters++;
        
    }
    references[eofCluster]->inDir = 1; // Include the EOF as being in the directory.
    references[eofCluster]->type = get_cluster_type(eofCluster, image_buf, bpb);
    return numClusters;
}

void orphan_fixer(uint8_t *image_buf, struct bpb33* bpb, struct node *references[], 
                                                                int numDataClusters) {
    //int is_orphan = 0; // 0 = not orphan, 1 = orphan.
    printf("For ref, valid clusts can go from 2-%d\n", (CLUST_LAST&FAT12_MASK));
    printf("EOF range is from %d-%d\n", (CLUST_EOFS&FAT12_MASK), (CLUST_EOFE&FAT12_MASK));
    int numClusters = 1;
    char name[64];
    char num[32];
    int orphanNum = 1;
    uint16_t nextCluster;
    struct direntry *dirent = (struct direntry*)cluster_to_addr(2, image_buf, bpb);
    nextCluster = get_fat_entry(2, image_buf, bpb);
    int clustType = get_cluster_type(2, image_buf, bpb);

    
    for (int i = 2; i < numDataClusters; i++) {
        if (nextCluster != 0) {
            printf("nextCluster: %d, inDir:%d, type: %d\n", nextCluster, references[i]->inDir, references[i]->type);
        }
        if (nextCluster != (FAT12_MASK&CLUST_FREE) && references[i]->inDir == 0) {
            printf("Orphan #%d found! Cluster #%d.\n", orphanNum, i);
            printf("Orphan type = %d\n", get_cluster_type(nextCluster, image_buf, bpb));
            printf("nextCluster: %d\n", nextCluster);
            sprintf(num, "%d", orphanNum); // Converts to string so we can concat.
            strcpy(name, "found");
            strcat(name, num);
            strcat(name, ".dat");
            while (!is_end_of_file(nextCluster)) {
                printf("Longer than two.\n");
                nextCluster = get_fat_entry(i, image_buf, bpb);
                references[i]-> inDir = 1;
                printf("Part of orphan: %d\n", i);
                numClusters++;
                i++;
            }
            create_dirent(dirent, name, i, numClusters * 512, image_buf, bpb);
            printf("Name: %s, i: %d, size: %d\n", name, i, numClusters*512);
            numClusters = 1;
            orphanNum++;
            references[i]->inDir = 1;
            references[i]->type = clustType;
            printf("Orphan fixed!\n");
        }
        nextCluster = get_fat_entry(i, image_buf, bpb);
        clustType = get_cluster_type(2, image_buf, bpb);
    }
}

// fixes the situation where a FAT chain is shorter than the expected filesize
void dir_entry_fixer(struct direntry *dirent, int chainLength) {
    uint32_t size = chainLength * 512;
    putulong(dirent->deFileSize, size);
}

// fixes the situation where a FAT chain is longer than the correct file size
void fat_chain_fixer(uint16_t startCluster, uint8_t *image_buf, struct bpb33* bpb, uint32_t expectedChainLength, struct node *references[]) {
    int currentNum = 1;
    uint16_t prevCluster = startCluster;
    uint16_t nextCluster = get_fat_entry(startCluster, image_buf, bpb);
    //currentNum++; // SAM ADDED THIS
    
    // cycle through the chain until the stopping point
    while (currentNum < expectedChainLength) {
        prevCluster = nextCluster;
        nextCluster = get_fat_entry(nextCluster, image_buf, bpb);
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
        set_fat_entry(toFree, CLUST_FREE, image_buf, bpb);
    }

    // frees the old EOF
    //printf("nextCluster: %d\n", nextCluster);
    references[nextCluster]->inDir = 0;
    references[nextCluster]->type = 0;
    set_fat_entry(nextCluster, CLUST_FREE, image_buf, bpb);
    //nextCluster = get_fat_entry(nextCluster, image_buf, bpb);
    //printf("nextCluster: %d\n", nextCluster);

    
    //printf("prevCluster: %d\n", prevCluster);
    // set the new last cluster to EOF
    set_fat_entry(prevCluster, (FAT12_MASK & CLUST_EOFS), image_buf, bpb);
    //update references
    references[prevCluster]->type = 2;
    references[prevCluster]->inDir = 1;
    prevCluster = get_fat_entry(prevCluster, image_buf, bpb);
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
            if (is_file(dirent, indent)) {
                // dirent is for a regular file
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
                        printf("Inconsistency fixed.\n");
                    }
                    else {
                        dir_entry_fixer(dirent, chainLength);
                        printf("Inconsistency fixed.\n");
                    }
                } else {
                    //printf("expected chain length (%u clusters) matches length of cluster chain (%d clusters)\n", expectedChainLength, chainLength);
                }
            }
            if (followclust) {
                // dirent is for a directory
                follow_dir(followclust, indent+1, image_buf, bpb, references);
            }
            dirent++;
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
        uint16_t followclust = print_dirent(dirent, 0);
        if (is_valid_cluster(followclust, bpb)) {            
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
    for (int i = 2; i < numDataClusters + 2; i ++) {
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
    
    orphan_fixer(image_buf, bpb, references, numDataClusters);



    unmmap_file(image_buf, &fd);
    for (int i = 2; i < 200; i++) {
        //printf("Cluster Number: %d; inFat: %d; inDir: %d; type: %d\n", i, references[i]->inFat,references[i]->inDir, references[i]->type);
        free(references[i]);
    }
    // remember to FREE references
    return 0;
}

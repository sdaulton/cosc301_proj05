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

static int dirint = 0;

void usage(char *progname) {
    fprintf(stderr, "usage: %s <imagename>\n", progname);
    exit(1);
}


// Modified by Sam Daulton, taken from is_valid_cluster in dos.c
int is_valid_cluster_correct(uint16_t cluster, struct bpb33 *bpb)
{
    uint16_t max_cluster = bpb->bpbSectors - 1 - 9 - 9 - 14;
    if (cluster >= (FAT12_MASK & CLUST_FIRST) && 
        cluster <= (FAT12_MASK & CLUST_LAST) &&
        cluster < max_cluster)
        return TRUE;
    return FALSE;
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

// Taken from dos_ls.c
void print_indent(int indent)
{
    int i;
    for (i = 0; i < indent*4; i++)
        printf(" ");
}

// From dos_ls.c
int get_name(char *fullname, struct direntry *dirent) 
{
    char name[9];
    char extension[4];
    int i;

    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);

    if (((uint8_t)name[0]) == SLOT_DELETED) {
	return -1;
    }
    /* names are space padded - remove the padding */
    for (i = 8; i > 0; i--) 
    {
    if (name[i] == ' ') 
        name[i] = '\0';
    else 
        break;
    }

    /* extensions aren't normally space padded - but remove the
       padding anyway if it's there */
    for (i = 3; i > 0; i--) 
    {
    if (extension[i] == ' ') 
        extension[i] = '\0';
    else 
        break;
    }
    
    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN){
        return -1;
    }
    
    fullname[0]='\0';
    /* append the extension if it's not a directory */
    if ((dirent->deAttributes & ATTR_DIRECTORY) == 0) 
    {
        if ((dirent->deAttributes & ATTR_HIDDEN) == ATTR_HIDDEN) {
            return -1;
        }
        strcat(fullname, name);
        strcat(fullname, ".");
        strcat(fullname, extension);
        return 0;
    } 
    strcat(fullname, name);
    return 0;
}


// modified by Sam Daulton & Bria Vicenti from dos_ls.c
uint16_t print_dirent(struct direntry *dirent, int indent, int thisDirint)
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
            dirint++;
            printf("%s/ (in directory #%d)\n", name, thisDirint);
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

        printf("%s.%s (%u bytes) (starting cluster %d) (in directory %d) %c%c%c%c\n",
               name, extension, size, getushort(dirent->deStartCluster), thisDirint,
               ro?'r':' ',
               hidden?'h':' ',
               sys?'s':' ',
               arch?'a':' ');
    }
    
    return followclust;
}

// based off print_dirent in dos_ls.c, modified by Sam Daulton
// returns 1 if it is a normal file or directory, 0 if not
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
    }
    else if ((dirent->deAttributes & ATTR_VOLUME) != 0)
    {
        // do nothing
    }
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0)
    {
        //do nothing
        if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN) {
            isNormalFile = 1;
        }
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

// Written by Sam Daulton
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

//Written by Sam Daulton -- features code from print_dirent in dos_ls.c
//fix the cluster already used in a cluster chain (either this file or another file) -->truncate this file to end at the cluster preceding the already used cluster
void fixUsedCluster(uint16_t prevCluster, uint16_t nextCluster, uint8_t *image_buf, struct bpb33* bpb, struct node *references[], struct direntry *dirent) {
    char name[9];
    // already in the cluster chain of another dirent
    // set prevCluster in chain as EOF
    name[8] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    /* names are space padded - remove the spaces */
    for (int i = 8; i > 0; i--)
    {
        if (name[i] == ' ')
            name[i] = '\0';
        else
            break;
    }
    printf("Cluster Number %d is already part of cluster chain.  So file %s was truncated to end at the cluster preceding %d\n", nextCluster, name, nextCluster);
    references[prevCluster]->type = 2;
    set_fat_entry(prevCluster, (FAT12_MASK & CLUST_EOFS), image_buf, bpb);
}


//Written by Sam Daulton
// Takes the start cluster number as a parameter and returns the length of the cluster chain (i.e. number of clusters in file)
int get_chain_length(uint16_t startCluster, uint8_t *image_buf, struct bpb33* bpb, struct node *references[], struct direntry *dirent) {
    int numClusters = 1;
    uint16_t nextCluster = get_fat_entry(startCluster, image_buf, bpb);
    uint16_t prevCluster = startCluster;
    uint16_t beforePrevCluster = startCluster;
    while (is_valid_cluster_correct(nextCluster, bpb)) {
		if (references[nextCluster]->inDir) {
        	fixUsedCluster(prevCluster, nextCluster, image_buf, bpb, references, dirent);
        	return numClusters;
        }
            
        references[nextCluster]->inDir = 1;
        references[nextCluster]->count = 1;
        references[nextCluster]->type = get_cluster_type(nextCluster, image_buf, bpb);
        beforePrevCluster = prevCluster;
        prevCluster = nextCluster;
        nextCluster = get_fat_entry(nextCluster, image_buf, bpb);
        numClusters++;
        
    }
    if (get_cluster_type(prevCluster, image_buf, bpb) == 3) {
            // nextCluster is bad
            // set previous to EOF
            // free next cluster
            // NOTE rest of chain still exists, we will make them orphans if they are valid fat entries.
            // If they we find a "bad orphan" we will free it.
            printf("Bad cluster: number: %d.  File truncated to cluster before bad cluster (now file size is %d bytes)\n", prevCluster, numClusters * 512);
            set_fat_entry(beforePrevCluster, (FAT12_MASK & CLUST_EOFS), image_buf, bpb);
            references[prevCluster]->inDir = 0;
            references[prevCluster]->type = 0;
            set_fat_entry(prevCluster, CLUST_FREE, image_buf, bpb);
            references[beforePrevCluster]->type = 2;
            return numClusters-1;
        } else if (nextCluster == 0) {
            //Empty
            references[prevCluster]->inDir = 1;
            references[prevCluster]->type = 2;
            set_fat_entry(prevCluster, (FAT12_MASK & CLUST_EOFS), image_buf, bpb);
            return numClusters+1;
        }

    return numClusters;
}

// Writen By Bria Vicenti
// checks if a direntry is valid. 0 if yes, -1 if no.
int is_valid_dir(struct direntry *dirent) {
    int valid = 0;
    uint32_t size = getulong(dirent->deFileSize);
    uint16_t startCluster = getushort(dirent->deStartCluster);

    // check 1: is the startcluster in a valid range?
    if (startCluster < 2 || startCluster > (FAT12_MASK & CLUST_EOFE) || 
                                                startCluster == ((FAT12_MASK & CLUST_BAD)) ) {
        valid = -1;
    }
    // check 2: is the size valid?
    else if (size <= 0) {
        valid = -1;
    }
    return valid;
}

// Written By Bria Vicenti
// given duplicate clusters n1 & n2, resolves it. 
// returns 0 if not deleted, return 1 if direntry deleted
int duplicate_fixer(uint8_t *image_buf, struct bpb33* bpb, struct node *references[], 
                                                                        int dup, struct direntry *dirent) 
{
    char newName[128];
    
    if (is_valid_dir(dirent) == -1) {
        dirent->deName[0] = SLOT_DELETED;
        printf("Duplicate is corrupt! Deleting now.\n");
        return 1;
    }
    
    printf("Two valid duplicates found! Scandisk will save one as a copy.\n");
    get_name(newName, dirent);
    char *p = strchr(newName, '.');
    strcpy(p, "2\0"); // Adds "2" to mark the duplicate and gets rid of the extension
    memcpy(dirent->deName, newName, strlen(newName));
    return 0;
}

// Written by Bria Vicenti
// Given a filename, checks for a duplicate of that filename and returns the clust. #
// if it exists.
int duplicate_finder(struct node* references[], char* filename, int numDataClusters, int n) {
    for (int i = 2; i < numDataClusters; i++) {
        if (strlen(filename) < 1) {
            return 0;
        }
        else if (strlen(references[i]->filename) < 1) {
            continue;
        }
        if (strcasecmp(filename, references[i]->filename) == 0) {
            if ((references[i]->dirint != references[n]->dirint) ||(references[i]->dirint == -1)||(references[n]->dirint == -1)) {
                // The two files are named the same but are in different directories.
                return 0;
            }
            printf("Duplicate found! There are two files named '%s'.\n", filename);
            return i;
        }
    }
    return 0;
}

// Written by Bria Vicenti
// detects and fixes any orphan clusters
void orphan_fixer(uint8_t *image_buf, struct bpb33* bpb, struct node *references[], 
                                                int numDataClusters) 
{
    int numClusters = 1;
    char name[64];
    char num[32];
    int orphanNum = 1;
    uint16_t nextCluster;
    struct direntry *dirent = (struct direntry*)cluster_to_addr(0, image_buf, bpb);
    int clustType = 0;

    for (int i = 2; i < numDataClusters; i++) {
        nextCluster = get_fat_entry(i, image_buf, bpb);        
        clustType = get_cluster_type(i, image_buf, bpb);

        if (nextCluster != (FAT12_MASK&CLUST_FREE) && references[i]->inDir == 0) {
            //orphan
            if (nextCluster == (FAT12_MASK & CLUST_BAD)) {
                //bad orphan
                //free it
                printf("Bad Orphan found! Cluster #%d. Fat Entry set to free.\n", i);
                set_fat_entry(nextCluster, CLUST_FREE, image_buf, bpb);
                continue;
            }
            printf("Orphan #%d found! Cluster #%d.\n", orphanNum, i);
            sprintf(num, "%d", orphanNum); // Converts to string so we can concat.
            strcpy(name, "found");
            strcat(name, num);
            strcat(name, ".dat");

            create_dirent(dirent, name, i, numClusters * 512, image_buf, bpb);
            int dup = 0;
            dup = duplicate_finder(references, name, numDataClusters, i); // check for duplicates
            if (dup != 0) {
                duplicate_fixer(image_buf, bpb, references, dup, dirent); // fix
            }

            numClusters = 1;
            orphanNum++;
            references[i]->inDir = 1;
            //references[i]->dirint = 0;

            // set type to eof
            // i.e. make the orphan cluster a standalone data file
            set_fat_entry(i, (FAT12_MASK & CLUST_EOFS), image_buf, bpb);
            references[i]->type = 2;
            printf("Orphan fixed!\n");
        }
    }
}

// Written by Bria Vicenti
// fixes the situation where a FAT chain is shorter than the expected filesize
void dir_entry_fixer(struct direntry *dirent, int chainLength) {
    uint32_t size = chainLength * 512;
    putulong(dirent->deFileSize, size);
}

// Written by Bria Vicenti,
// fixes the situation where a FAT chain is longer than the correct file size
void fat_chain_fixer(uint16_t startCluster, uint8_t *image_buf, struct bpb33* bpb, uint32_t expectedChainLength, struct node *references[]) {
    int currentNum = 1;
    uint16_t prevCluster = startCluster;
    uint16_t nextCluster = get_fat_entry(startCluster, image_buf, bpb);
    
    // cycle through the chain until the stopping point
    while (currentNum < expectedChainLength) {
        prevCluster = nextCluster;
        nextCluster = get_fat_entry(nextCluster, image_buf, bpb);
        currentNum++;
    }
    
    // free any clusters past the correct size
    if (is_valid_cluster_correct(nextCluster,bpb)) {
    	while (!is_end_of_file(get_fat_entry(nextCluster, image_buf, bpb))) {
        	
            uint16_t toFree = nextCluster;

        	//update references
        	references[nextCluster]->inDir = 0;
        	references[nextCluster]->type = 0;
        	nextCluster = get_fat_entry(nextCluster, image_buf, bpb);
        	set_fat_entry(toFree, CLUST_FREE, image_buf, bpb);
    	}
    

    	// frees the old EOF
    	references[nextCluster]->inDir = 0;
    	references[nextCluster]->type = 0;
    	set_fat_entry(nextCluster, CLUST_FREE, image_buf, bpb);
    }
    // set the new last cluster to EOF
    set_fat_entry(prevCluster, (FAT12_MASK & CLUST_EOFS), image_buf, bpb);

    //update references
    references[prevCluster]->type = 2;
    references[prevCluster]->inDir = 1;
    prevCluster = get_fat_entry(prevCluster, image_buf, bpb);
}

// Written by Sam Daulton
//function that checks the size of the dirent compared to the length of the cluster chain and calls the appropriate fixer function if inconsistent
void check_size(struct direntry* dirent, uint8_t *image_buf, struct bpb33* bpb, struct node *references[], int numDataClusters, int thisDirint) {
    uint32_t size = 0;
    uint16_t startCluster = 0;
    uint32_t expectedChainLength = 0;
    int chainLength = 0;
    size = getulong(dirent->deFileSize);
    startCluster = getushort(dirent->deStartCluster);
    int dup = 0;	
    
    // update cluster references
    char name[128];
    if(get_name(name, dirent) == -1) {
    	return;
    }
    
    //check if start cluster is valid
	if (!is_valid_cluster_correct(startCluster, bpb)) {
        // start cluster num is not valid
         printf("Start Cluster Number %d is not valid.  So file %s was deleted\n", startCluster, name);
        dirent->deName[0] = SLOT_DELETED;
        return;
    }

    dup = duplicate_finder(references, name, numDataClusters, startCluster); // check for duplicates & fix
    if (dup != 0) {
        duplicate_fixer(image_buf, bpb, references, dup, dirent);
        return; // Just stop the operation because this direntry is no longer relevant.
    }

    strcpy(references[startCluster]->filename, name);

    if (references[startCluster]->inDir) {
        printf("Start Cluster Number %d is already part of cluster chain.  So file %s was deleted\n", startCluster, name);
        dirent->deName[0] = SLOT_DELETED;
        return;
    } else {
        references[startCluster]->inDir = 1;
        references[startCluster]->dirint = thisDirint;
        references[startCluster]->type = get_cluster_type(startCluster, image_buf, bpb);
    }

    // check that length of cluster chain == size
    // this also checks if any clusters in this dirent's cluster chain are already part of a cluster chain.
    chainLength = get_chain_length(startCluster, image_buf, bpb, references, dirent);
    // ceiling division
    expectedChainLength = (size % 512) ? (size / 512 + 1) : (size / 512);
    if (expectedChainLength == 0) {
        // directories have expected length 0 clusters, but they still use 1
        expectedChainLength = 1;
    }
    if (chainLength != expectedChainLength) {
        printf("INCONSISTENCY: expected chain length (%u clusters) does not match length of cluster chain (%d clusters)\n", expectedChainLength, chainLength);
        if (chainLength > expectedChainLength) {
            fat_chain_fixer(startCluster, image_buf, bpb, expectedChainLength, references);
            printf("Inconsistency now fixed.\n");
        }
        else {
            dir_entry_fixer(dirent, chainLength);
            printf("Inconsistency now fixed.\n");
        }
    }
}

// from dos_ls.c, modified by Sam Daulton
void follow_dir(uint16_t cluster, int indent,
    uint8_t *image_buf, struct bpb33* bpb, struct node *references[], int numDataClusters)
{
	int thisDirint = dirint; // the directory number for this directory
    while (is_valid_cluster_correct(cluster, bpb))
    {
        struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
        
        int numDirEntries = (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) / sizeof(struct direntry);
        int i = 0;
        for ( ; i < numDirEntries; i++)
        {
            // for each file in this directory
            uint16_t followclust = print_dirent(dirent, indent, thisDirint);
            if (is_file(dirent, indent)) {
                // check size and fix inconsistency if necessary
                check_size(dirent, image_buf, bpb, references, numDataClusters, thisDirint);
            }
            if (followclust) {
                // dirent is for a directory
                follow_dir(followclust, indent+1, image_buf, bpb, references, numDataClusters);
            }
            dirent++;
        }
        
        cluster = get_fat_entry(cluster, image_buf, bpb);
    }
}

//from dos_ls.c modified by Sam Daulton
void traverse_root(uint8_t *image_buf, struct bpb33* bpb, struct node *references[], int numDataClusters)
{
    uint16_t cluster = 0;
    struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
    
    int i = 0;
    for ( ; i < bpb->bpbRootDirEnts; i++)
    {
        uint16_t followclust = print_dirent(dirent, 0, 0);
        if (is_file(dirent, 0)) {
            check_size(dirent, image_buf, bpb, references, numDataClusters, 0);
        }
        if (is_valid_cluster_correct(followclust, bpb)) {            
            follow_dir(followclust, 1, image_buf, bpb, references, numDataClusters);
        }
        dirent++;
    }
}

// Written by Sam Daulton and Bria Vicenti
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
    struct node *references[numDataClusters]; // only + 2 to create idempotent mapping from cluster number to index
    for (int i = 2; i < numDataClusters; i ++) {
        references[i] = malloc(sizeof(struct node));
        node_init(references[i]);
    }

    // traverse directory entries to gather metadata
    traverse_root(image_buf, bpb, references, numDataClusters);
    
    // find and fix orphans    
    orphan_fixer(image_buf, bpb, references, numDataClusters);

    unmmap_file(image_buf, &fd);
    for (int i = 2; i < numDataClusters; i++) {        
        free(references[i]);
    }

    free(bpb);
    return 0;
}

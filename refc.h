#ifndef __REFC_H__
#define __REFC_H__

struct node {
	int inFat;
	int inDir;
    int type; // 0 -> empty; 1-> normal part of cluster chain; 2-> eof; 3-> bad; 4 -> reserved; -1 -> not set
};

void node_init(struct node *n);

#endif // __REFC_H__

#ifndef __REFC_H__
#define __REFC_H__

struct node {
	int inFat;
	int inDir;
	int isValid;
	int isFull;
};

void node_init(struct node *n);

#endif // __REFC_H__

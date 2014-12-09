#include "refc.h"

void node_init(struct node *n) {
	n -> inFat = 0;
	n -> inDir = 0;
    n -> type = -1;
}
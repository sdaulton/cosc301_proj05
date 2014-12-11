#include "refc.h"

void node_init(struct node *n) {
	n -> inDir = 0;
    n -> type = -1;
    n -> count = 0;
    n -> dirint = -1;
}
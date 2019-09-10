#include <mpi.h>
#include <omp.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define NUM_LOCKS 1000
#define ARY_SIZE 13337
#define LOCK_CONST (13336 / NUM_LOCKS + 1)
#define NUM_RECEIVERS 10

typedef struct list {
	char* str;
	struct list* next;
} list_t;

typedef struct map {
	char* str;
	int count;
	struct map* next;
} map_t;


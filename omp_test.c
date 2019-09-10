#include <stdio.h>
#include <omp.h>
#include <math.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>

#define NUM_LOCKS 1000
#define ARY_SIZE 13337
#define LOCK_CONST (13336 / NUM_LOCKS + 1)

typedef struct list {
	char* str;
	struct list* next;
} list_t;

typedef struct map {
	char* str;
	int count;
	struct map* next;
} map_t;

void divideWork(list_t**, int);
void push(list_t**, char*);
void read(list_t**, map_t**, omp_lock_t*);
char* pop(list_t**);
int hashStr(char*);
void mapPrint(map_t**);

int main(int argc, char* argv[]) {
	int i;
	int numThreads;
#pragma omp parallel
#pragma omp single
	numThreads = omp_get_num_threads();

	omp_lock_t locks[NUM_LOCKS];
	for(i = 0; i < NUM_LOCKS; i++) {
		omp_init_lock(locks + i);	
	}
	list_t** workList = calloc(numThreads, sizeof(*workList));
	map_t** wordMap = calloc(sizeof(*wordMap),  ARY_SIZE);
	
	//Divide the work evenly among the files
	divideWork(workList, numThreads);
	double t_start = omp_get_wtime();

	//Parallel loop that starts all the combination readers/mappers/reducers
	#pragma omp parallel
	{
		read(workList, wordMap, locks);
		printf("Thread %d: Finishing after %lf\n", omp_get_thread_num(), omp_get_wtime() - t_start);
	}
	mapPrint(wordMap);
	printf("Time taken on %d nodes: %lf\n", numThreads, omp_get_wtime() - t_start);
	return 0;
}

//Prints out the map
void mapPrint(map_t** wordMap) {
	int i;
	map_t* mapLoc;
	FILE* file = fopen("output/OMP_Output.txt", "w");
	for(i = 0; i < ARY_SIZE; i++) {
		mapLoc = wordMap[i];
		while(mapLoc != NULL) {
			fprintf(file, "<\"%s\",%d>\n", mapLoc->str, mapLoc->count);
			mapLoc = mapLoc->next;
		}
	}
	fclose(file);
}

//Push something onto the map
void mapPush(map_t** wordMap, char* word, omp_lock_t* locks) {
	int hashVal = hashStr(word);
	map_t* mapLoc = wordMap[hashVal];
	map_t* mapPrev = wordMap[hashVal];
	
	//Locks a small region of the map and then writes what it wants to, then releases the lock. I believe hash is pretty well distributed so I didnt think there would be a lot of waiting here
	omp_set_lock(locks + hashVal / LOCK_CONST);
	while(mapLoc != NULL) {
		if(!strcmp(mapLoc->str, word)) {
			mapLoc->count = mapLoc->count + 1;
			omp_unset_lock(locks + hashVal / LOCK_CONST);
			return;
		}
		mapPrev = mapLoc;
		mapLoc = mapLoc->next;
	}
	mapLoc = malloc(sizeof(*mapLoc));
	mapLoc->str = malloc(sizeof(*(mapLoc->str)) * (strlen(word) + 1));
	strcpy(mapLoc->str, word);
	mapLoc->count = 1;
	mapLoc->next = NULL;
	if(mapPrev == NULL) {
		wordMap[hashVal] = mapLoc;
	} else {
		mapPrev->next = mapLoc;
	}
	omp_unset_lock(locks + hashVal / LOCK_CONST);
}

void divideWork(list_t** workList, int numProcs) {
	DIR* d;
	struct dirent* dir;
	int work[24] = {0};
	int size;
	int min;
	int i;
	char* dirpath = malloc(sizeof(*dirpath) * 40);
	FILE* fp;
	d = opendir("./OMP_WORK");
	if(d) {
		int procNum = 0;
		while((dir = readdir(d)) != NULL) {
			char* extension = strrchr(dir->d_name, '.');
			if(extension && !strcmp(extension, ".txt")) {
				snprintf(dirpath, 40, "./OMP_WORK/%s", dir->d_name);	
				fp = fopen(dirpath, "r");
				fseek(fp, 0L, SEEK_END);
				size = ftell(fp);
				fclose(fp);
				min = 0;
				for(i = 1; i < numProcs; i++) {
					if(work[i] < work[min]) {
						min = i;
					}
				}
				for(i = 0; i < 100; i++) {
					push(&workList[min], dirpath);
					work[min] += size;
				}
			}

		}
		closedir(d);
	}
}

void push(list_t** head, char* str) {
	list_t* new;
	new = malloc(sizeof(*new));
	new->str = malloc(sizeof(*(new->str)) * strlen(str));
	strcpy(new->str, str);
	new->next = *head;
	*head = new;
}

char* pop(list_t** head) {
	if(*head == NULL) {
		return NULL;
	}

	list_t* next = (*head)->next;
	char* retStr = malloc(sizeof((*head)->str) * strlen((*head)->str));
	strcpy(retStr, (*head)->str);

	free((*head)->str);
	free(*head);
	*head = next;
	return retStr;
}

void read(list_t** workList, map_t** wordMap, omp_lock_t* locks) {
	int myID = omp_get_thread_num();
	char* filename;
	int i;
	int k;
	while((filename = pop(&workList[myID])) != NULL) {
		//printf("ID %d has %s\n", myID, filename);
		FILE* fp = fopen(filename, "r");
		char c;
		char tempBuff[50];
		char prevChar = '\0';
		int wordLen = 0;
		int doubledash;
		//Opens a file and starts going through the word
		while((c = fgetc(fp)) != EOF) {
			//Builds the word
			if(wordLen <= 49 && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')  || (c == '-' && prevChar != '-'))) {	  
				if(c >= 'A' && c <= 'Z') c += 'a' - 'A'; // make asme case 
				tempBuff[wordLen++] = c;
			} else if (wordLen > 0) { // This is when I add the word to the map
				if(c == '-' && prevChar == '-') { // Acounts for hyphanated things
					tempBuff[wordLen - 1] = '\0';
				}
				tempBuff[wordLen] = '\0';
				if(tempBuff[0] != '\0') {
					mapPush(wordMap, tempBuff, locks); // Pushes word onto the shared map
				}
				wordLen = 0;
			}
			prevChar = c;
		}
		free(filename);
	}
}

int hashStr(char* str) {
	int i = 0;
	int hashVal = 0;
	while(str[i] != '\0') {
		hashVal += str[i] * pow(31, i % 5);
		i++;
	}
	return hashVal % 13337;
}


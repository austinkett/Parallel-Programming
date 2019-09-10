#include "mpiTest.h"

int main (int argc, char *argv[]) {
	int provided;
	omp_set_num_threads(16);
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);                 /* start MPI           */
	/* MPI Parameters                 */
	int rank, size;
	char name[MPI_MAX_PROCESSOR_NAME];
	int i;

	/* OpenMP Parameters */
	int id, numThreads;

	MPI_Comm_size(MPI_COMM_WORLD, &size);   /* get number of ranks */
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);   /* get rank            */

#pragma omp parallel
#pragma omp single
	numThreads = omp_get_num_threads();

	omp_lock_t locks[NUM_LOCKS];
	for(i = 0; i < NUM_LOCKS; i++) {
		omp_init_lock(locks + i);	
	}
	list_t** workList = calloc(numThreads, sizeof(*workList));
	map_t** wordMap = calloc(sizeof(*wordMap), ARY_SIZE);
	double t_start = omp_get_wtime();

#pragma omp parallel private(i, numThreads)
	{
		int a;
		for(a = 0; a < 256 / size; a++) {
			int k;
			int wordLen = 0;
			int doubledash;
			char prevChar = '\0';
			char tempBuff[50];
			char c;
			map_t* mapLoc;
			map_t* mapPrev;
			int myID = omp_get_thread_num();
			char* myFilename = malloc(sizeof(*myFilename) * 40);
			snprintf(myFilename, 40, "./OMP_WORK/ex_%d.txt", myID + 1); 
			FILE* fp = fopen(myFilename, "r");
			while((c = fgetc(fp)) != EOF) {
				//Builds the word
				if(wordLen <= 49 && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')  || (c == '-' && prevChar != '-'))) {
					if(c >= 'A' && c <= 'Z') c += 'a' - 'A'; // make all lowercase
					tempBuff[wordLen++] = c;
				} else if (wordLen > 0) { // This is when I add the word to the map
					if(c == '-' && prevChar == '-') { // Acounts for hyphanated things
						tempBuff[wordLen - 1] = '\0';
					}
					tempBuff[wordLen] = '\0';
					if(tempBuff[0] != '\0') {
						int hashVal = 0;
						i = 0;
						while(tempBuff[i] != '\0') {
							hashVal += tempBuff[i] * pow(31, i % 5);
							i++;
						}
						hashVal %= 13337;
						mapLoc = wordMap[hashVal];
						mapPrev = wordMap[hashVal];

						//Locks a small region of the map and then writes what it wants to, then releases the lock.
						int skip = 0;
						omp_set_lock(locks + hashVal / LOCK_CONST);
						while(mapLoc != NULL) {
							if(!strcmp(mapLoc->str, tempBuff)) {
								mapLoc->count = mapLoc->count + 1;
								omp_unset_lock(locks + hashVal / LOCK_CONST);
								skip = 1;
								break;
							}
							mapPrev = mapLoc;
							mapLoc = mapLoc->next;
						}
						if(skip == 0) {
							mapLoc = malloc(sizeof(*mapLoc));
							mapLoc->str = malloc(sizeof(*(mapLoc->str)) * (strlen(tempBuff) + 1));
							strcpy(mapLoc->str, tempBuff);
							mapLoc->count = 1;
							mapLoc->next = NULL;
							if(mapPrev == NULL) {
								wordMap[hashVal] = mapLoc;
							} else {
								mapPrev->next = mapLoc;
							}
							omp_unset_lock(locks + hashVal / LOCK_CONST);
						}
					}
					wordLen = 0;
				}
				prevChar = c;
			}
		}
	}
	printf("MPI %d Nodes: Mapping finished in %le seconds\n", size, omp_get_wtime() - t_start);
	map_t** newMap = calloc(sizeof(*newMap), ARY_SIZE);
#pragma omp parallel
	{
#pragma omp single
		for(i = 0; i < numThreads; i++) {
			if(i < NUM_RECEIVERS) { //RECEIVERS
#pragma omp task firstprivate(i)
				{
					char* buff = malloc(sizeof(char) * 58);
					char* tBuff = malloc(sizeof(char) * 50);
					char doneMsg[50] = "duun";
					int counts;
					int loc;
					int skip;
					int doneCount = (numThreads - NUM_RECEIVERS) * size;
					map_t* mapLoc;
					map_t* mapPrev;
					MPI_Status stats;
					while(doneCount) {
						MPI_Recv(buff, 58, MPI_BYTE, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &stats);
						memcpy(tBuff, buff, 50);
						memcpy(&counts, buff + 50, 4);
						memcpy(&loc, buff + 54, 4);
						if(strcmp(tBuff, doneMsg) == 0) {
							doneCount--;
							continue;
						}
						skip = 0;
						mapLoc = newMap[loc];
						mapPrev = NULL;
						omp_set_lock(locks + loc / LOCK_CONST);	
						while(mapLoc != NULL) {
							if(!strcmp(mapLoc->str, tBuff)) {
								mapLoc->count = mapLoc->count + counts;
								omp_unset_lock(locks + loc / LOCK_CONST);	
								skip = 1;
								break;
							}
							mapPrev = mapLoc;
							mapLoc = mapLoc->next;
						}
						if(skip == 0) {
							mapLoc = malloc(sizeof(*mapLoc));
							mapLoc->str = malloc(sizeof(*(mapLoc->str)) * (strlen(tBuff) + 1));
							strcpy(mapLoc->str, tBuff);
							mapLoc->count = counts;
							mapLoc->next = NULL;
							if(mapPrev == NULL) {
								newMap[loc] = mapLoc;
							} else {
								mapPrev->next = mapLoc;
							}
							omp_unset_lock(locks + loc / LOCK_CONST);	
						}
					}	
				}
			} else { //SENDERS
#pragma omp task firstprivate(i)
				{
					int l;
					map_t* mapPos;
					char* strBuff = malloc(sizeof(char) * 58);
					int min = (i - NUM_RECEIVERS) * (ARY_SIZE / (16 - NUM_RECEIVERS) + 1);
					int max = (i - NUM_RECEIVERS) == (numThreads - NUM_RECEIVERS - 1) ? ARY_SIZE : (i - NUM_RECEIVERS + 1) * (ARY_SIZE / (16 - NUM_RECEIVERS) + 1);
					for(l = min; l < max; l++) {
						mapPos = wordMap[l];
						while(mapPos != NULL) {
							memcpy(strBuff, mapPos->str, 50);
							memcpy(strBuff + 50, &(mapPos->count), 4);
							memcpy(strBuff + 54, &l, 4);
							int hash = 0;
							int m = 0;
							while(strBuff[m] != '\0') {
								hash += strBuff[m] * pow(31, m % 5);
								m++;
							}
							hash %= 13337;

							MPI_Send(strBuff, 58, MPI_BYTE, hash / (ARY_SIZE / size + 1), 0, MPI_COMM_WORLD);
							mapPos = mapPos->next;
						}
					}
					char doneMsg[58] = "duun";
					int r;
					for(l = 0; l < size; l++) {
						for(r = 0; r < NUM_RECEIVERS; r++) {
							MPI_Send(doneMsg, 58, MPI_BYTE, l, 0, MPI_COMM_WORLD);
						}
					}
				}
			}
		}
	}
	if(rank == 0) {
		FILE* file = fopen("output/OMP_Output.txt", "w");
		map_t* mapLocc;	
		for(i = 0; i < ARY_SIZE; i++) {
			fprintf(file, "Hash: %d\n", i);
			mapLocc = newMap[i];
			while(mapLocc != NULL) {
				fprintf(file, "<\"%s\",%d>\n", mapLocc->str, mapLocc->count);
				mapLocc = mapLocc->next;
			}
		}
		fclose(file);
	}
	printf("MPI %d Nodes: Finished redistribution in %le seconds\n", size, omp_get_wtime() - t_start);

	MPI_Finalize();                         /* terminate MPI       */
	return 0;
}

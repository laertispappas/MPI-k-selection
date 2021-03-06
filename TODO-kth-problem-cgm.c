#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "vector.h" 

#define SOLUTION_FOUND 1


void generate_numbers(IntVectorPtr vector, int size){     
	int i;
	srand(time(NULL));

	for (i = 0; i < size; i++){
		VecAdd(vector, rand() % 99999999 + 1);
	}
}

void print_numbers(int *arrNumbers, int size){
	int i;
	for (i = 0; i < size; i++){
		printf("%d \n", arrNumbers[i]);
	}
}

void vecPrint(IntVectorPtr vector){
	int i;

	int size;
	size = VecGetSize(vector);	
	for(i=0;i<size;i++)
		printf("%d %c",VecGet(vector,i),(i == size-1) ? '\n' : ' ');
}

int main(int argc, char **argv){
	double startTime, stopTime;

	int total_final_size = 0; // for step 3
	int FOUND = FALSE;
	int STOP_PROCESSES = 0;

	int solution;
	int world_size, world_rank;
	const int c = 500;
	const int MAX_NUMBERS = 100000000;

	int N;
	int k = 150;
	int median;

	IntVectorPtr pVec = VecNew(MAX_NUMBERS);

	MPI_Init(&argc, &argv);	
	MPI_Comm_size(MPI_COMM_WORLD, &world_size);// get world size

	if (world_size < 2){
		fprintf(stderr, "Must use at least two processes for CGM Algorithm \n");
		MPI_Abort(MPI_COMM_WORLD, 1);
	}
	
	MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);// get rank

	// generate numbers on master process or seq
	if (world_rank == 0) {
		int i;
		generate_numbers(pVec, MAX_NUMBERS); /////////
//		for(i = MAX_NUMBERS; i > 0; i--){
//			VecAdd(pVec, rand() + 1); // max to min 999, 998, 997, 996 ....
//			VecAdd(pVec, i); // max to min 999.., ..998, ..997, 996 ....1
//		}
	}
	
	int receivcounts[world_size];
	int displs[world_size];

	startTime = MPI_Wtime();

	// 1. Set N = n
	N = MAX_NUMBERS;

	int size = N / world_size;
	int remaining_elements = MAX_NUMBERS % world_size;         ///////////

	int sizev[world_size];
	int start_displs = 0;
	int i;

	for (i = 0; i < world_size; i++)
	{
		if (i < remaining_elements) {
			sizev[i] = size + 1;
	  	}
		else {
    		sizev[i] = size;
		}  

		//using the DISPLS array to keep track of the relative starting locations of the chunks
		displs[i] = start_displs;  
		start_displs = start_displs + sizev[i];
	}
	IntVectorPtr local_pVec = VecNew(sizev[world_rank]);

	MPI_Scatterv(pVec->data, sizev, displs, MPI_INT, local_pVec->data, sizev[world_rank], MPI_INT, 0, MPI_COMM_WORLD);

	local_pVec->size = sizev[world_rank];

	// todo on step 2.2
	int medians_ni[2];
	int master_medians_ni[world_size*2];

	int medians[world_size];
	int ni[world_size];
	int *M = (int *)malloc(sizeof(int));

	VecQuickSort(local_pVec);

	int from, to, current_size;
	from = 0;
	to = current_size = local_pVec->size;

	// 2. Repeat untill N <= n / (cp)
	while(N >= (MAX_NUMBERS / (c * world_size))){

	// 2.1 Each processor i compute the median mi of its ni elements
		if (local_pVec->size % 2 == 0){
			median = (local_pVec->data[local_pVec->size / 2] + local_pVec->data[local_pVec->size / 2 - 1]) / 2;
			medians_ni[0] = median;
		}else {
			median = local_pVec->data[local_pVec->size / 2];
			medians_ni[0] = median;
		}
		medians_ni[1] = local_pVec->size;

		// 2.2 Each processor i sends mi and ni to processor 1
		MPI_Gather(&median, 1, MPI_INT, medians, 1, MPI_INT, 0, MPI_COMM_WORLD);
		MPI_Gather(&local_pVec->size, 1, MPI_INT, ni, 1, MPI_INT, 0, MPI_COMM_WORLD);  ////////na ginei ena dianisma?

		// 2.3 Processor 1 computes the weighted median M
		if (world_rank == 0){
			int i, j;
			int min_sum = 0;
			int max_sum = 0;
			int found = FALSE;
			
			for (i = 0; i < world_size; i++){
				int mk = medians[i];
				for(j = 0; j < world_size; j++){
					if (medians[j] < mk){
						min_sum += ni[j];
					}else if(medians[j] > mk){
						max_sum += ni[j];
					}
				}
				if ((min_sum <= (N/2)) && (max_sum <= (N/2))){
					M = &medians[i];
					found = TRUE;
					break;
				}
				min_sum = 0; 
				max_sum = 0;
			}
			if (found==FALSE){
				M = &medians[0];
			}
		}

		// 2.4 Processor 1 broadcasts M to all other processors
		MPI_Bcast(M, 1, MPI_INT, 0, MPI_COMM_WORLD);

		// 2.5 Each processor i compute li, ei, gi respectively the numbers of its local elements less than, equal to, or greater than M
		int i;
		int send_leg[3] = {0, 0, 0};
		int LEG[3] = {0, 0, 0};

		for(i = 0; i < local_pVec->size; i++){
			if (local_pVec->data[i] < *M){
				send_leg[0] += 1;
			}
			else if (local_pVec->data[i] > *M){
				send_leg[2] += 1;
			}
			else{
				send_leg[1] += 1;
			}
		}

		// 2.6  Each processor i sends li, ei, gi to processor 1
		// 2.7 Processor 1 computes L=SUM(li), E = SUM(ei), G=SUM(gi) total number of elements less than equal to or greater than M
		// 2.8 Processor 1 broadcasts L,E,G to all other processes
		MPI_Allreduce(send_leg, LEG, 3, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

		// 2.9 One of the following
		//	 	if L < k <= L + E then return solution M and stop //////////
		if((k > LEG[0]) && (k <= (LEG[0] + LEG[1]))) {
			solution = *M;
			FOUND = TRUE;
			STOP_PROCESSES = 1;
	
			// found stop
			break;
		}

		//		if k <= L then each processor i discards all but those elements less than M and set N = L
		else if(k <= LEG[0]){
			int i;
			for(i = 0; i < local_pVec->size; i++){
				if (local_pVec->data[i] >= *M) {
					VecErase(local_pVec, i);
					i--;
				}
			}
			N = LEG[0];
		}
		//		if k > L + E then each processor i discards all but those elements greater than M and set N = G and k = k - L - E
		else if(k > LEG[0] + LEG[1]){
			int i;
			for(i = 0; i < local_pVec->size; i++){
				if (local_pVec->data[i] <= *M){
					VecErase(local_pVec, i);
					i--;
				}
			}
			N = LEG[2];
			k = k - LEG[0] - LEG[1];
		}

		if(FOUND){
			break;
		}
		if(STOP_PROCESSES){
			break;
		}
	} // end while

	if(!FOUND) {
		// 3. All the remaining elements are sent to processor 1
		
		int *recvcounts = (int *)malloc(sizeof(int)*world_size);
		int *final_displs = (int *)malloc(sizeof(int)*world_size);

		// gather each vectors size to recvcounts
		MPI_Gather(&local_pVec->size, 1, MPI_INT, recvcounts, 1,  MPI_INT, 0, MPI_COMM_WORLD);
		int i;

		if(world_rank == 0)	
			for(i = 0; i < world_size; i++){
				total_final_size += recvcounts[i];
			}

		VecDelete(pVec);

		if(world_rank == 0) {
			IntVectorPtr pVec = VecNew(total_final_size);
		}

		// get displs
		int start;
		if(world_rank == 0) {
			final_displs[0] = 0;
			start = recvcounts[0];

			for(i=1; i < world_size; ++i){
				final_displs[i] = start;
				start += recvcounts[i];
			}
		}

		// w8 for proc 0 to get displacements and recv counts
		MPI_Barrier(MPI_COMM_WORLD);
		MPI_Gatherv(local_pVec->data, local_pVec->size, MPI_INT, pVec->data, recvcounts, final_displs, MPI_INT, 0, MPI_COMM_WORLD);

		if(world_rank==0)
			pVec->size = total_final_size;

		// 4. Processor 1 solves the remaining problem sequentially
		if(world_rank == 0){
			VecQuickSort(pVec);
			solution = VecGet(pVec, k - 1);
			stopTime = MPI_Wtime();
			printf("kth element=%d \ntime: %f\n", solution, stopTime - startTime);		
		}
		if(world_rank==0)
			VecDelete(pVec);
		VecDelete(local_pVec);
	}
	else {
		if(world_rank == 0) {
			stopTime = MPI_Wtime();
			printf("kth element %d\n time: %f\n", *M, stopTime - startTime);
		}
		VecDelete(pVec);
		VecDelete(local_pVec);
	}
	
	MPI_Finalize();
}

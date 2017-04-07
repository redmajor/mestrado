#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include "pq-utils/pq_assign.h"
#include "pq-utils/pq_new.h"
#include "pq-utils/pq_search.h"
#include "pq-utils/pq_test_compute_stats.h"

int main(int argv, char** argc){
	struct timeval inicio, final;

	int nsq,
		k,
		tmili,
		*ids;

	float* dis;
	char* dataset ;
			
	matI codebook;
	data v;
	pqtipo pq;
	int tam;

	if(argv < 2){
		cout << "Usage: ./pq_test <dataset>" << endl;
		return -1;
	}

	dataset=argc[1];
	tam=atoi(argc[2]);

	gettimeofday(&inicio, NULL);
	v = pq_test_load_vectors(dataset,tam,0);
	gettimeofday(&final, NULL);
	tmili = (int) (1000 * (final.tv_sec - inicio.tv_sec) + (final.tv_usec - inicio.tv_usec) / 1000);
	printf("Tempo load vectors: %d\n", tmili);
	
	nsq=8;
	k=100;
	dis = (float*)malloc(sizeof(float)*v.query.n*k);
	ids = (int*)malloc(sizeof(int)*v.query.n*k);

	gettimeofday(&inicio, NULL);
	pq = pq_new(nsq, v.train);
	gettimeofday(&final, NULL);
	tmili = (int) (1000 * (final.tv_sec - inicio.tv_sec) + (final.tv_usec - inicio.tv_usec) / 1000);
	printf("Tempo new: %d\n", tmili);

	for(int i=0; i<(pq.centroidsn*pq.centroidsd/4); i++){
		printf("%g ", pq.centroids[i]);
	}

	gettimeofday(&inicio, NULL);
	codebook = pq_assign(pq, v.base);
	gettimeofday(&final, NULL);
	tmili = (int) (1000 * (final.tv_sec - inicio.tv_sec) + (final.tv_usec - inicio.tv_usec) / 1000);
	printf("Tempo assign: %d\n", tmili);

	gettimeofday(&inicio, NULL);
	pq_search(pq, codebook, v.query, k, dis , ids);
	gettimeofday(&final, NULL);
	tmili = (int) (1000 * (final.tv_sec - inicio.tv_sec) + (final.tv_usec - inicio.tv_usec) / 1000);
	printf("Tempo search: %d\n", tmili);

	pq_test_compute_stats (ids, v.ids_gnd,k);

	free(dis);
	free(ids);
	free(pq.centroids);
	free(codebook.mat);
	free(v.base.mat);
	free(v.query.mat);
	free(v.train.mat);

	return 0;
}

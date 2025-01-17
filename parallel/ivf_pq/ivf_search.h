#ifndef H_IVFSEARCH
#define H_IVFSEARCH

	#include <stdio.h>
	#include <stdlib.h>
	#include <mpi.h>
	#include <sys/time.h>
	#include <omp.h>
	#include <list>
	#include <float.h>
	#include <semaphore.h>
	#include <string.h>
	extern "C"{
		#include "../yael_needs/vector.h"
		#include "../yael_needs/nn.h"
		#include "../yael_needs/kmeans.h"
	}
	#include "../pq-utils/pq_test_load_vectors.h"
	#include "../pq-utils/pq_new.h"
	#include "../pq-utils/pq_search.h"
	#include "ivf_training.h"
	#include "myIVF.h"
	#include "k_min.h"

	typedef struct ivf_threads{
		ivf_t *ivf;
		ivfpq_t ivfpq;
		int threads;
		int thread;
		mat residual;
	}ivf_threads_t;

	void parallel_search (int nsq, int k, int comm_sz, int threads, int tam, MPI_Comm search_comm, char *dataset, int w);
	void send_aggregator(int residualn, int w, query_id_t *fila, int **ids, float **dis, int finish_aux, int count);
	ivf_t* create_ivf(ivfpq_t ivfpq, int threads, int tam, int my_rank, int nsq, char* dataset);
	void merge_ivf(ivf_t *ivf, ivf_t ivf2);
	ivf_t* write_ivf(ivfpq_t ivfpq, int threads, int tam, int my_rank, int nsq, char*dataset);
	ivf_t* read_ivf(ivfpq_t ivfpq, int tam, int my_rank);
	dis_t ivfpq_search(ivf_t *ivf, float *residual, pqtipo pq, int centroid_idx, double *g1, double *g2);
	int min(int a, int b);
	float * sumidxtab2(mat D, matI ids, int offset);
	int* imat_new_transp (const int *a, int ncol, int nrow);

#endif

#include "ivf_search.h"

#include <sys/time.h>
#include <set>
#include <cstdio>
#include <queue>
#include <ctime>

#include "mycuda.h"

#include <cmath>

#include "pqueue.h"

#include "debug.h"

static int last_assign, last_search, last_aggregator;
static sem_t sem;


//TODO: dont try this at home
time_t start, end;
double micro;


struct timeval tv;
struct timeval start_tv;
struct timeval old_start_tv;

#ifdef DEBUG
#define sw(call)  old_start_tv = start_tv; \
				  gettimeofday(&start_tv, NULL); \
				  call ; \
				  gettimeofday(&tv, NULL); \
				  micro = (tv.tv_sec - start_tv.tv_sec) * 1000000 + (tv.tv_usec - start_tv.tv_usec); \
				  printf ("Elapsed: %.2lf seconds on call: %s\n", micro / 1000000, #call); \
				  start_tv = old_start_tv;
#else
#define sw(call) call;
#endif

struct timeval total_tv;
struct timeval total_start_tv;

//TODO: refactor variable names, they are terrible
//TODO: comment the code
//TODO: think about how to parallelize the other stages and/or if it would be worthwhile

float dist2(float* a, float* b, int size) {
	float d = 0;
	for (int i = 0; i < size; i++) {
		float diff = a[i] - b[i];
		d += diff * diff;
	}

	return d;
}

//TODO: we dont need k, maybe we shouldnt require that core_cpu and core_gpu have the same interface. Or maybe we should create some sort of structure that represents the context info
//TODO: receive the number of threads from outside sources
void core_cpu(pqtipo PQ, mat residual, ivf_t* ivf, int ivf_size, int* rid_to_ivf, int* qid_to_starting_outid, matI idxs, mat dists, int k, int w) {
	const int threads = 8;
	debug("Executing %d threads", threads);

	#pragma omp parallel num_threads(threads)
	{
		int nthreads = omp_get_num_threads();
		int tid = omp_get_thread_num();

		for (int qid = tid; qid < residual.n / w; qid += nthreads) {
			int rid = qid * w;
			
			std::pair<float, int> heap[k];
			pqueue pq(heap, k);
			
			for (int i = 0; i < w; i++, rid++) {
				float* current_residual = residual.mat + rid * PQ.nsq * PQ.ds;

				mat distab;
				distab.mat = new float[PQ.ks * PQ.nsq];
				distab.n = PQ.nsq;
				distab.d = PQ.ks;

				for (int d = 0; d < PQ.nsq; d++) {
					for (int k = 0; k < PQ.ks; k++) {
						float dist = dist2(current_residual + d * PQ.ds, PQ.centroids + d * PQ.ks * PQ.ds + k * PQ.ds, PQ.ds);
						distab.mat[PQ.ks * d + k] = dist;
					}
				}

				int ivf_id = rid_to_ivf[rid];
				ivf_t entry = ivf[ivf_id];


				for (int j = 0; j < entry.idstam; j++) {
					float dist = 0;

					for (int d = 0; d < PQ.nsq; d++) {
						dist += distab.mat[PQ.ks * d + entry.codes.mat[PQ.nsq * j + d]];
					}

					pq.add(dist, entry.ids[j]);
				}

				delete[] distab.mat;
			}
			
			int outid = qid_to_starting_outid[qid];
			
			for (int i = 0; i < pq.size; i++) {
				dists.mat[outid] = pq.base[i].first;
				idxs.mat[outid] = pq.base[i].second;
				outid++;
			}
		}
	}
}

//TODO: try to refactor the code so that we don't need to have these "partial" arrays, which are basically a copy of the full array
void do_on(void (*target)(pqtipo, mat, ivf_t*, int, int*, int*, matI, mat, int, int),
		ivfpq_t PQ, std::list<int>& queries, mat residual, int* coaidx, ivf_t* ivf, query_id_t*& elements, matI& idxs, mat& dists, int k, int w) {
//	std::set<int> coaidPresent;

	if (queries.size() == 0) return;
	
//	int D = residual.d;

//	mat partial_residual;
//	partial_residual.n = queries.size() * w;
//	partial_residual.d = residual.d;
//	partial_residual.mat = new float[partial_residual.d * partial_residual.n];

	elements = new query_id_t[queries.size()];

//	for (int rid = 0; rid < w * queries.size(); rid++) {
//		coaidPresent.insert(coaidx[rid]);
//		for (int d = 0; d < D; d++) {
//			partial_residual.mat[rid * D + d] = residual.mat[rid * D + d];
//		}
//	}

//	ivf_t partial_ivf[coaidPresent.size()];
//	std::map<int, int> coaid_to_IVF;
//
//	int i = 0;
//	for (int coaid : coaidPresent) {
//		partial_ivf[i].idstam = ivf[coaid].idstam;
//		partial_ivf[i].ids = ivf[coaid].ids;
//		partial_ivf[i].codes = ivf[coaid].codes;
//		coaid_to_IVF.insert(std::pair<int, int>(coaid, i));
//		i++;
//	}

//	int rid_to_ivf[queries.size() * w];
	int qid_to_starting_outid[queries.size()];

	int rid = 0;
	int outid = 0;
	for (int qid = 0; qid < queries.size(); qid++) {
		int numImgs = 0;

		for (int i = 0; i < w; i++, rid++) {
//			rid_to_ivf[rid] = coaid_to_IVF.find(coaidx[rid])->second;
			numImgs += ivf[coaidx[rid]].idstam;
		}

		int size = min(numImgs, k);
		elements[qid].id = qid;
		elements[qid].tam = size;
		qid_to_starting_outid[qid] = outid;
		outid += size;
	}

	idxs.mat = new int[outid];
	idxs.n = outid;
	dists.mat = new float[outid];
	dists.n = outid;

//	if (partial_residual.n >= 1) {
	sw((*target)(PQ.pq, residual, ivf, PQ.coa_centroidsn, coaidx, qid_to_starting_outid, idxs, dists, k, w));
//	}

//	delete[] partial_residual.mat;
}


void do_cpu(ivfpq_t PQ, std::list<int>& to_cpu, mat residual, int* coaidx, ivf_t* ivf, query_id_t*& elements, matI& idxs, mat& dists, int k, int w) {
	do_on(&core_cpu, PQ, to_cpu, residual, coaidx, ivf, elements, idxs, dists, k, w);
}

void do_gpu(ivfpq_t PQ, std::list<int>& to_gpu, mat residual, int* coaidx, ivf_t* ivf,  query_id_t*& elements, matI& idxs, mat& dists, int k, int w) {
	do_on(&core_gpu, PQ, to_gpu, residual, coaidx, ivf, elements, idxs, dists, k, w);
}

void send_results(int nqueries, query_id_t* elements, matI idxs, mat dists, int finish) {
	int counter = 0;

	int my_rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

	MPI_Send(&my_rank, 1, MPI_INT, last_aggregator, 1, MPI_COMM_WORLD);
	MPI_Send(&nqueries, 1, MPI_INT, last_aggregator, 0, MPI_COMM_WORLD);
	MPI_Send(elements, sizeof(query_id_t) * nqueries, MPI_BYTE, last_aggregator,
			0, MPI_COMM_WORLD);

	MPI_Send(idxs.mat, idxs.n, MPI_INT, last_aggregator, 0,
	MPI_COMM_WORLD);
	MPI_Send(dists.mat, dists.n, MPI_FLOAT, last_aggregator, 0,
	MPI_COMM_WORLD);
	MPI_Send(&finish, 1, MPI_INT, last_aggregator, 0, MPI_COMM_WORLD);
}



void parallel_search (int nsq, int k, int comm_sz, int threads, int tam, MPI_Comm search_comm, char *dataset, int w){
	ivfpq_t ivfpq;
	mat residual;
	int *coaidx, my_rank;
	//double time;

	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

	set_last (comm_sz, &last_assign, &last_search, &last_aggregator);

	//Recebe os centroides
	MPI_Recv(&ivfpq, sizeof(ivfpq_t), MPI_BYTE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	ivfpq.pq.centroids = (float*)malloc(sizeof(float)*ivfpq.pq.centroidsn*ivfpq.pq.centroidsd);
	MPI_Recv(&ivfpq.pq.centroids[0], ivfpq.pq.centroidsn*ivfpq.pq.centroidsd, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	ivfpq.coa_centroids=(float*)malloc(sizeof(float)*ivfpq.coa_centroidsd*ivfpq.coa_centroidsn);
	MPI_Recv(&ivfpq.coa_centroids[0], ivfpq.coa_centroidsn*ivfpq.coa_centroidsd, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	std::cout << "number of coarse centroids: " << ivfpq.coa_centroidsn << "\n";
	std::cout << "number of product centroids per dimension: " << ivfpq.pq.centroidsn << "\n";
	std::cout << "number of product centroids dimensions: " << ivfpq.pq.centroidsd << "\n";


	ivf_t *ivf, *ivf2;

	#ifdef WRITE_IVF
		debug("STEP1: CREATING IVF");
		write_ivf(ivfpq, threads, tam, my_rank, nsq, dataset);
		debug("STEP2: READING IVF");
		ivf = read_ivf(ivfpq, tam, my_rank);
	#else
		#ifdef READ_IVF
			ivf = read_ivf(ivfpq, tam, my_rank);

		#else
			ivf = create_ivf(ivfpq, threads, tam, my_rank, nsq, dataset);
		#endif
	#endif

	float **dis;
	int **ids;
	int finish_aux=0;

	int count = 0;


	int base_id = 0; // corresponds to the query_id

	MPI_Barrier(search_comm);

	sem_init(&sem, 0, 1);
	
	preallocate_gpu_mem(ivfpq.pq, ivf, ivfpq.coa_centroidsn);

	gettimeofday(&total_start_tv, NULL);


	while (1) {
		MPI_Bcast(&residual.n, 1, MPI_INT, 0, search_comm);
		MPI_Bcast(&residual.d, 1, MPI_INT, 0, search_comm);

		residual.mat = (float*) malloc(sizeof(float) * residual.n * residual.d);

		MPI_Bcast(&residual.mat[0], residual.d * residual.n, MPI_FLOAT, 0,
				search_comm);

		coaidx = (int*) malloc(sizeof(int) * residual.n);

		MPI_Bcast(&coaidx[0], residual.n, MPI_INT, 0, search_comm);
		MPI_Bcast(&finish_aux, 1, MPI_INT, 0, search_comm);

		dis = (float**) malloc(sizeof(float *) * (residual.n / w));
		ids = (int**) malloc(sizeof(int *) * (residual.n / w));

		std::list<int> to_gpu;
		std::list<int> to_cpu;

		debug("residual.n=%d", residual.n);

		for (int qid = 0;  qid < residual.n / w;  qid++) {
			to_gpu.push_back(qid);
		}

		debug("EXECUTING ON THE %s", to_cpu.size() == 0 ? "gpu" : "cpu");

		time_t start,end;
		time (&start);

		debug("PQ.ks=%d and k=%d", ivfpq.pq.ks, k);

		//GPU PART
		query_id_t* elements;
		matI idxs;
		mat dists;
		
		if (to_gpu.size() != 0) {
			sw(do_gpu(ivfpq, to_gpu, residual, coaidx, ivf, elements, idxs, dists, k, w));
		} else {
			sw(do_cpu(ivfpq, to_cpu, residual, coaidx, ivf, elements, idxs, dists, k, w));
		}

		debug("Before sending results");
		sw(send_results(to_cpu.size() + to_gpu.size(), elements, idxs, dists, finish_aux));
		debug("after sending results");
		
		delete[] elements;
		delete[] idxs.mat;
		delete[] dists.mat;

		base_id += residual.n / w;


		if (finish_aux == 1) break;

		std::cout << "ABORTTTTTTTTTT THIS SHIT\n";
	}

	gettimeofday(&total_tv, NULL);
	micro = (total_tv.tv_sec - total_start_tv.tv_sec) * 1000000 + (total_tv.tv_usec - total_start_tv.tv_usec); \
	printf("\nElapsed: %.2lf seconds on TOTAL\n", micro / 1000000);

	deallocate_gpu_mem();
	
	debug("GOT OUT OF HERE");
	
	sem_destroy(&sem);
	free(ivf);
	free(ivfpq.pq.centroids);
	free(ivfpq.coa_centroids);

	debug("FINISHED THE SEARCH");
}

ivf_t* create_ivf(ivfpq_t ivfpq, int threads, int tam, int my_rank, int nsq, char* dataset){
	ivf_t *ivf;
	struct timeval start, end;
	double time;
	int lim;

	debug("Indexing");

	gettimeofday(&start, NULL);

	ivf = (ivf_t*)malloc(sizeof(ivf_t)*ivfpq.coarsek);
	for(int i=0; i<ivfpq.coarsek; i++){
		ivf[i].ids = (int*)malloc(sizeof(int));
		ivf[i].idstam = 0;
		ivf[i].codes.mat = (int*)malloc(sizeof(int));
		ivf[i].codes.n = 0;
		ivf[i].codes.d = nsq;
	}
	lim = tam/1000000;
	if(tam%1000000!=0){
		lim = (tam/1000000) + 1;
	}

	tam = (tam - 1) % 1000000 + 1;

	//Cria a lista invertida correspondente ao trecho da base assinalado a esse processo
	#pragma omp parallel for num_threads(threads) schedule(dynamic)
		for(int i=0; i<lim; i++) {
			ivf_t *ivf2;
			int aux;
			mat vbase;
			ivf2 = (ivf_t *)malloc(sizeof(ivf_t)*ivfpq.coarsek);

			vbase = pq_test_load_base(dataset, i, my_rank-last_assign, tam);

			ivfpq_assign(ivfpq, vbase, ivf2);

			for(int j=0; j<ivfpq.coarsek; j++){
				for(int l=0; l<ivf2[j].idstam; l++){
					ivf2[j].ids[l]+=1000000*i+tam*(my_rank-last_assign-1);
				}

				aux = ivf[j].idstam;
				#pragma omp critical
				{
					ivf[j].idstam += ivf2[j].idstam;
					ivf[j].ids = (int*)realloc(ivf[j].ids,sizeof(int)*ivf[j].idstam);
					memcpy (ivf[j].ids+aux, ivf2[j].ids, sizeof(int)*ivf2[j].idstam);
					ivf[j].codes.n += ivf2[j].codes.n;
					ivf[j].codes.mat = (int*)realloc(ivf[j].codes.mat,sizeof(int)*ivf[j].codes.n*ivf[j].codes.d);
					memcpy (ivf[j].codes.mat+aux*ivf[i].codes.d, ivf2[j].codes.mat, sizeof(int)*ivf2[j].codes.n*ivf2[j].codes.d);
				}
				free(ivf2[j].ids);
				free(ivf2[j].codes.mat);
			}
			free(vbase.mat);
			free(ivf2);
		}

	gettimeofday(&end, NULL);
	time = ((end.tv_sec * 1000000 + end.tv_usec)-(start.tv_sec * 1000000 + start.tv_usec))/1000;

	debug("Tempo de criacao da lista invertida: %g",time);

	return ivf;
}

void write_ivf(ivfpq_t ivfpq, int threads, int tam, int my_rank, int nsq, char* dataset){
	FILE *fp;
	char name_arq[100];
	struct timeval start, end;
	double time;
	
	debug("Indexing");

	gettimeofday(&start, NULL);

	int lim = tam / 1000000;
	if (tam % 1000000 != 0) lim++;

	//Cria a lista invertida correspondente ao trecho da base assinalado a esse processo
	#pragma omp parallel for num_threads(threads) schedule(dynamic)
		for (int i = 0; i < lim; i++) {
			ivf_t* ivf = (ivf_t *) malloc(sizeof(ivf_t)*ivfpq.coarsek);
			mat vbase = pq_test_load_base(dataset, i, my_rank - last_assign, tam);

			ivfpq_assign(ivfpq, vbase, ivf);

			free(vbase.mat);

			for (int j = 0; j < ivfpq.coarsek; j++) {
				for (int l = 0; l < ivf[j].idstam; l++) ivf[j].ids[l] += 1000000 * i + tam * (my_rank - last_assign - 1);

				#pragma omp critical
				{
					sprintf(name_arq, "/home/rafael/mestrado/parallel2/ivf/ivf_%d_%d_%d.bin", ivfpq.coarsek, tam, j);
					fp = fopen(name_arq,"ab");
					fwrite(&ivfpq.coarsek, sizeof(int), 1, fp);
					fwrite(&ivf[j].idstam, sizeof(int), 1, fp);
					fwrite(&ivf[j].ids[0], sizeof(int), ivf[j].idstam, fp);
					fwrite(&ivf[j].codes.n, sizeof(int), 1, fp);
					fwrite(&ivf[j].codes.d, sizeof(int), 1, fp);
					fwrite(&ivf[j].codes.mat[0], sizeof(int), ivf[j].codes.n*ivf[j].codes.d, fp);
					fclose(fp);
				}
				
				free(ivf[j].ids);
				free(ivf[j].codes.mat);
			}
			free(ivf);
		}

	gettimeofday(&end, NULL);
	time = ((end.tv_sec * 1000000 + end.tv_usec)-(start.tv_sec * 1000000 + start.tv_usec))/1000;

	debug("\nTempo de criacao da lista invertida: %g",time);
}

//TODO: make these paths available in a config file
//TODO: make the decision of wheter reading or writing the IVF a runtime option
ivf_t* read_ivf(ivfpq_t ivfpq, int tam, int my_rank){
	debug("READ_IVF called");
	
	ivf_t* ivf;
	FILE *fp;
	char name_arq[100];
	int coarsek;

	ivf = (ivf_t*)malloc(sizeof(ivf_t)*ivfpq.coarsek);
	
	debug("ivfpq.coarsek=%d", ivfpq.coarsek);

	int lim = tam / 1000000;
	if (tam % 1000000 != 0) lim++;

	
	for(int i = 0; i < ivfpq.coarsek; i++){
		int idstam, codesn, codesd;

		ivf[i].ids = (int*) malloc(sizeof(int));
		ivf[i].idstam = 0;
		ivf[i].codes.mat = (int*) malloc(sizeof(int));
		ivf[i].codes.n = 0;
		ivf[i].codes.d = ivfpq.pq.nsq;

		sprintf(name_arq, "/home/rafael/mestrado/parallel2/ivf/ivf_%d_%d_%d.bin", ivfpq.coarsek, tam, i);
//		debug("Opening %s", name_arq);
		fp = fopen(name_arq,"rb");

		for(int j=0; j<lim; j++){
			fread(&coarsek, sizeof(int), 1, fp);
//			debug("coarsek=%d", coarsek);
			fread(&idstam, sizeof(int), 1, fp);
//			debug("idstam=%d", idstam);
			ivf[i].idstam += idstam;
			ivf[i].ids = (int*)realloc(ivf[i].ids,sizeof(int)*ivf[i].idstam);
			fread(&ivf[i].ids[ivf[i].idstam-idstam], sizeof(int), idstam, fp);
			fread(&codesn, sizeof(int), 1, fp);
//			debug("codesn=%d", codesn);
			ivf[i].codes.n += codesn;
			fread(&codesd, sizeof(int), 1, fp);
//			debug("codesd=%d", codesd);
			ivf[i].codes.d = codesd;
			ivf[i].codes.mat = (int*)realloc(ivf[i].codes.mat,sizeof(int)*ivf[i].codes.n*ivf[i].codes.d);
			fread(&ivf[i].codes.mat[((ivf[i].codes.n)*(ivf[i].codes.d))-codesn*codesd], sizeof(int), codesn*codesd, fp);
		}
		fclose(fp);
	}

	return ivf;
}

int min(int a, int b){
	if(a>b){
		return b;
	}
	else
		return a;
}

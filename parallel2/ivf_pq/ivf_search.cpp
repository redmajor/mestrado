#include "ivf_search.h"

static int last_assign, last_search, last_aggregator;

void parallel_search (int nsq, int k, int comm_sz, int threads, MPI_Comm search_comm){

	ivfpq_t ivfpq;
	ivf_t *ivf;
	mat residual;
	int *coaidx;

	set_last (comm_sz, &last_assign, &last_search, &last_aggregator);

	//Recebe os centroides
	MPI_Recv(&ivfpq, sizeof(ivfpq_t), MPI_BYTE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	ivfpq.pq.centroids = (float*)malloc(sizeof(float)*ivfpq.pq.centroidsn*ivfpq.pq.centroidsd);
	MPI_Recv(&ivfpq.pq.centroids[0], ivfpq.pq.centroidsn*ivfpq.pq.centroidsd, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	ivfpq.coa_centroids=(float*)malloc(sizeof(float)*ivfpq.coa_centroidsd*ivfpq.coa_centroidsn);
	MPI_Recv(&ivfpq.coa_centroids[0], ivfpq.coa_centroidsn*ivfpq.coa_centroidsd, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	
	//Recebe o trecho da lista invertida assinalada ao processo
	ivf = (ivf_t*)malloc(sizeof(ivf_t)*ivfpq.coarsek);
	for(int i=0;i<ivfpq.coarsek;i++){
		
		MPI_Recv(&ivf[i].codes.d, 1, MPI_INT, 0, 0, MPI_COMM_WORLD,MPI_STATUS_IGNORE);

		MPI_Recv(&ivf[i].idstam, 1, MPI_INT, 0, 0, MPI_COMM_WORLD,MPI_STATUS_IGNORE);

		ivf[i].codes.n = ivf[i].idstam; 

		ivf[i].ids = (int*)malloc(sizeof(int)*ivf[i].idstam);
		MPI_Recv(&ivf[i].ids[0], ivf[i].idstam, MPI_INT, 0, 0, MPI_COMM_WORLD,MPI_STATUS_IGNORE);
		
		ivf[i].codes.mat = (int*)malloc(sizeof(int)*ivf[i].codes.n*ivf[i].codes.d);
		MPI_Recv(&ivf[i].codes.mat[0], ivf[i].codes.n*ivf[i].codes.d, MPI_INT, 0, 0, MPI_COMM_WORLD,MPI_STATUS_IGNORE);
		
	}
	
	int queryn;
	
	MPI_Barrier(search_comm);
	MPI_Bcast(&queryn, 1, MPI_INT, 0, search_comm);		
	MPI_Bcast(&residual.d, 1, MPI_INT, 0, search_comm);
    residual.n=queryn/1;
	residual.mat = (float*)malloc(sizeof(float)*residual.n*residual.d);
	int j=0;
	while(j<queryn){
	
		MPI_Bcast(&residual.mat[0], residual.d*residual.n, MPI_FLOAT, 0, search_comm);
		coaidx = (int*)malloc(sizeof(int)*residual.n);
		MPI_Bcast(&coaidx[0], residual.n, MPI_INT, 0, search_comm);

		# pragma omp parallel for num_threads(threads)
		
			for(int i=0; i<residual.n; i++){
				dis_t q = ivfpq_search(ivf, &residual.mat[0]+i*residual.d, ivfpq.pq, coaidx[i]);
		
				int ktmp = min(q.idx.n, k), id, my_rank;
				MPI_Request request;

				MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
				id = i+my_rank+1;

				int *ids;
				float *dis;

				ids = (int*) malloc(sizeof(int)*ktmp);
				dis = (float*) malloc(sizeof(float)*ktmp);
		
				my_k_min(q, ktmp, &dis[0], &ids[0]);
				
				# pragma omp critical
				{
					MPI_Send(&id, 1, MPI_INT, last_aggregator, 100000, MPI_COMM_WORLD);
					MPI_Send(&my_rank, 1, MPI_INT, last_aggregator, id, MPI_COMM_WORLD);
					MPI_Send(&ktmp, 1, MPI_INT, last_aggregator, id, MPI_COMM_WORLD);
					MPI_Send(&ids[0], ktmp, MPI_INT, last_aggregator, id, MPI_COMM_WORLD);
					MPI_Send(&dis[0], ktmp, MPI_FLOAT, last_aggregator, id, MPI_COMM_WORLD);
				}

				free(dis);
				free(ids);
				free(q.dis.mat);
				free(q.idx.mat);					
			}
			
		j+=residual.n;
	}

	free(residual.mat);
	free(ivfpq.pq.centroids);
	free(ivfpq.coa_centroids);
	free(ivf);
}

dis_t ivfpq_search(ivf_t *ivf, float *residual, pqtipo pq, int centroid_idx){
	dis_t q;
	int ds, ks, nsq;

	ds = pq.ds;
	ks = pq.ks;
	nsq = pq.nsq;

	mat distab;
	distab.mat = (float*)malloc(sizeof(float)*ks*nsq);
	distab.n = nsq;
	distab.d = ks;
	
	float *distab_temp=(float*)malloc(sizeof(float)*ks);
	
	float* AUXSUMIDX;
	
	q.dis.n = ivf[centroid_idx].codes.n;
	q.dis.d = 1;
	q.dis.mat = (float*)malloc(sizeof(float)*q.dis.n);

	q.idx.n = ivf[centroid_idx].codes.n;
	q.idx.d = 1;
	q.idx.mat = (int*)malloc(sizeof(int)*q.idx.n);
	
	for (int query = 0; query < nsq; query++) {
		compute_cross_distances(ds, 1, distab.d, &residual[query*ds], &pq.centroids[query*ks*ds], distab_temp);
		memcpy(distab.mat+query*ks, distab_temp, sizeof(float)*ks);
	}
	
	AUXSUMIDX = sumidxtab2(distab, ivf[centroid_idx].codes, 0);

	memcpy(q.idx.mat, ivf[centroid_idx].ids,  sizeof(int)*ivf[centroid_idx].idstam);
	memcpy(q.dis.mat, AUXSUMIDX, sizeof(float)*ivf[centroid_idx].codes.n);
	
	free (AUXSUMIDX);
	free (distab_temp);
	free (distab.mat);
	return q;
}

int min(int a, int b){
	if(a>b){
		return b;
	}
	else
		return a;
}

float * sumidxtab2(mat D, matI ids, int offset){
	//aloca o vetor a ser retornado
	float *dis = (float*)malloc(sizeof(float)*ids.n);
	int i, j;

	//soma as distancias para cada vetor
	for (i = 0; i < ids.n ; i++) {
		float dis_tmp = 0;
		for(j=0; j<D.n; j++){
			dis_tmp += D.mat[ids.mat[i*ids.d+j]+ offset + j*D.d];
		}
		dis[i]=dis_tmp;
	}

	return dis;
}

int* imat_new_transp (const int *a, int ncol, int nrow){
 	int i,j;
	int *vt=(int*)malloc(sizeof(int)*ncol*nrow);

 	for(i=0;i<ncol;i++)
    	for(j=0;j<nrow;j++)
   			vt[i*nrow+j]=a[j*ncol+i];

  	return vt;
}

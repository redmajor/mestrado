#include "pq_new.h"

/*
* nsq: numero de subquantizadores (m no paper)
* vtrain: estrutura de vetores, contendo os dados de treinamento
*/

pqtipo pq_new(int nsq, mat vtrain){

	/*
	*ds: dimensao dos subvetores
	*ks: numero de centroides por subquantizador
	*flags: modo de inicializacao do kmeans
	*assign: vetor que guarda os indices dos centroides
	*seed: semente do kmeans
	*centroids_tmp: vetor que guarda temporariamente os centroides de um subvetor
	*dis: vetor que guarda as distancias entre um centroide e o vetor assinalado por ele
	*vs: vetor que guarda temporariamente cada subvetor
	*pq: estrutura do quantizador
	*/

	int	i,
		j,
		ds,
		ks,
		flags,
		*assign,
		seed=1;

	float	*centroids_tmp,
			*dis,
			*vs;

	pqtipo pq;

	//definicao de variaveis

	flags = flags & KMEANS_INIT_RANDOM;
	ds=vtrain.d/nsq;
	ks=pow(2,nsq);
	pq.nsq = nsq;
	pq.ks = ks;
	pq.ds = ds;
	pq.centroids.n=ks;

	//alocacao de memoria
	
	centroids_tmp= fvec_new(ks);
	dis = fvec_new(pq.centroids.n);
	assign= ivec_new(pq.centroids.n);
	vs=fmat_new (ds, vtrain.n);

	for(i=0;i<nsq;i++){
		memcpy(vs,vtrain.mat+vtrain.n*ds*i, sizeof(float)*vtrain.n*ds);
		kmeans(ds, vtrain.n, ks, 100, vs, flags, seed, 1, centroids_tmp , dis, assign, NULL);
		fvec_concat(pq.centroids.mat, pq.centroids.n, centroids_tmp, ks);
		pq.centroids.n += ks;
	}

	return pq;
}

void check_new(){
  cout << ":::PQ_NEW OK:::" << endl;
}

void fvec_concat(float* vinout, int vinout_n, float* vin, int vin_n){
	if (vinout == NULL) {
		vinout = (float*)malloc(sizeof(float)*vin_n);
		vinout_n = 0;
		memcpy(vinout + vinout_n, vin, sizeof(float)*vin_n);
	}
	else{
		fvec_resize(vinout, vinout_n + vin_n);
		memcpy(vinout + vinout_n, vin, sizeof(float)*vin_n);
	}
}

void ivec_concat(int* vinout, int vinout_n, int* vin, int vin_n){
	if (vinout == NULL) {
		vinout = (int*)malloc(sizeof(int)*vin_n);
		vinout_n = 0;
		memcpy(vinout + vinout_n, vin, sizeof(int)*vin_n);
	}
	else{
		ivec_resize(vinout, vinout_n + vin_n);
		memcpy(vinout + vinout_n, vin, sizeof(int)*vin_n);
	}
}

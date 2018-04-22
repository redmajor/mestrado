#include <cuda_runtime.h>

#include "mycuda.h"
#include "topk.cu"

#include "helper_cuda.h"

#include <cstdio>

#define safe_call(call) if (cudaSuccess != call) { err = cudaGetLastError(); \
													fprintf(stderr, "Failed call: %s (error code %s)!\n", \
															#call, cudaGetErrorString(err)); \
													exit(EXIT_FAILURE); }


//TODO: remember to not execute queries that dont correspond to an entry on the problem

extern __shared__ char shared_memory[];

// PQ.ks * PQ.nsq must be a multiple of 1024
__global__ void compute_dists(pqtipo PQ, mat residual, ivf_t* ivf, int* entry_map, int* starting_imgid, int* starting_inputid, Img* original_input, matI idxs, mat dists) {
	int tid = threadIdx.x;
	int nthreads = blockDim.x;
	int qid = blockIdx.x;
	
	float* distab = (float*) shared_memory;
	
	//computing disttab
	int step = PQ.ks * PQ.nsq / nthreads; 
	int j = step * tid;
	int initial_d = j / PQ.ks;
	int initial_k = j % PQ.ks;
	
	int nd = max(step / PQ.ks, 1); //nd = 1
	int slice = min(step, PQ.ks); //slice = 2 
		
	float* current_residual = residual.mat + qid * PQ.nsq * PQ.ds;
	
	for (int d = initial_d; d < initial_d + nd; d++) {
		float* sub_residual = current_residual + d * PQ.ds;
		
		for (int k = initial_k; k < initial_k + slice; k++) {
			float* centroid = PQ.centroids + (d * PQ.ks + k) * PQ.ds;
			float dist = 0;

			for (int i = 0; i < PQ.ds; i++) {
				float diff = sub_residual[i] - centroid[i];
				dist += diff * diff;
			}
			
			distab[PQ.ks * d + k] = dist;
		}
		
		initial_k = 0;
	}
	
	__syncthreads();
	
	//computing the distances to the vectors
	ivf_t entry = ivf[entry_map[qid]];
	Img* input = original_input + starting_inputid[qid];
	
	int block_size = blockDim.x;

	for (int i = tid; i < entry.idstam; i += block_size) {
		float dist = 0;

		for (int s = 0; s < PQ.nsq; s++) {
			dist += distab[PQ.ks * s + entry.codes.mat[PQ.nsq * i + s]];
		}
		
		input[i] = { dist, entry.ids[i] };
	}
	
	__syncthreads();

	// now selecting num_shards
	auto shared_memory_size = 48 << 10 - PQ.ks * PQ.nsq * sizeof(float);  // 48 KB
	auto heap_size = PQ.ks * sizeof(Entry<Img>);
	// shared_memory_size = (num_shards + 1) * heap_size <=>
	int num_shards = shared_memory_size / heap_size - 1;
	if (num_shards <= 0) {
		num_shards = 1;
	}
	auto shard_size = entry.idstam / num_shards;
	auto min_shard_size = 2 * PQ.ks;
	if (shard_size < min_shard_size) {
		num_shards = entry.idstam / min_shard_size;
	}
	if (num_shards <= 0) {
		num_shards = 1;
	} else if (num_shards > 1024) {
		num_shards = 1024;
	}

	topk(num_shards, PQ.ks, original_input, starting_inputid, dists.mat, idxs.mat);
}



void core_gpu(pqtipo PQ, mat residual, ivf_t* ivf, int ivf_size, int* entry_map, int* starting_imgid,  int* starting_inputid,  int num_imgs, matI idxs, mat dists) {
	//print lengths
//	for (int i = 0; i < residual.n; i++) {
//		std::printf("LENGTH[%d]=%d\n", i, starting_inputid[i+1] - starting_inputid[i]);
//		std::printf("LENGTH#2[%d]=%d\n", i, ivf[entry_map[i]].idstam);
//	}
	
	
	
	//TODO: implement / redo the error handling so that we have less code duplication
	cudaError_t err = cudaSuccess;
	
	pqtipo gpu_PQ = PQ;
	safe_call(cudaMalloc((void **) &gpu_PQ.centroids, sizeof(float) * PQ.centroidsd * PQ.centroidsn));
	safe_call(cudaMemcpy(gpu_PQ.centroids, PQ.centroids, sizeof(float) * PQ.centroidsd * PQ.centroidsn, cudaMemcpyHostToDevice));
	
	mat gpu_residual = residual;
	safe_call(cudaMalloc((void **) &gpu_residual.mat, sizeof(float) * residual.n * residual.d));
	safe_call(cudaMemcpy(gpu_residual.mat, residual.mat, sizeof(float) * residual.n * residual.d, cudaMemcpyHostToDevice));
	
	
	ivf_t* gpu_ivf;
	safe_call(cudaMalloc((void **) &gpu_ivf, sizeof(ivf_t) * ivf_size));
	
	ivf_t* tmp_ivf = new ivf_t[ivf_size];
	
	for (int i = 0; i < ivf_size; i++) {
		tmp_ivf[i].idstam = ivf[i].idstam; 
		tmp_ivf[i].codes = ivf[i].codes;
		
		safe_call(cudaMalloc((void **) &tmp_ivf[i].ids, sizeof(int) * tmp_ivf[i].idstam));
		safe_call(cudaMemcpy(tmp_ivf[i].ids, ivf[i].ids, sizeof(int) * ivf[i].idstam, cudaMemcpyHostToDevice));
		
		safe_call(cudaMalloc((void **) &tmp_ivf[i].codes.mat, sizeof(int) * tmp_ivf[i].codes.n * tmp_ivf[i].codes.d));
		safe_call(cudaMemcpy(tmp_ivf[i].codes.mat, ivf[i].codes.mat, sizeof(int) * ivf[i].codes.n * ivf[i].codes.d, cudaMemcpyHostToDevice));
	}
	
	safe_call(cudaMemcpy(gpu_ivf, tmp_ivf, sizeof(ivf_t) * ivf_size, cudaMemcpyHostToDevice));
	
	int* gpu_entry_map;
	safe_call(cudaMalloc((void **) &gpu_entry_map, sizeof(int) * residual.n));
	safe_call(cudaMemcpy(gpu_entry_map, entry_map, sizeof(int) * residual.n, cudaMemcpyHostToDevice));
	
	int* gpu_starting_imgid;
	safe_call(cudaMalloc((void **) &gpu_starting_imgid, sizeof(int) * residual.n));
	safe_call(cudaMemcpy(gpu_starting_imgid, starting_imgid, sizeof(int) * residual.n, cudaMemcpyHostToDevice));
	
	//query_id_t* gpu_elements;
	//safe_call(cudaMalloc((void **) &gpu_elements, sizeof(query_id_t) * residual.n)); //TODO: I dont know if this is truly needed
	//safe_call(cudaMemcpy(gpu_elements, elements, sizeof(query_id_t) * residual.n, cudaMemcpyHostToDevice));// TODO: need to rethink this
	
	matI gpu_idxs = idxs;
	
	
	safe_call(cudaMalloc((void **) &gpu_idxs.mat, sizeof(int) * idxs.n));
	std::printf("Allocating: %dMB\n", sizeof(int) * idxs.n / 1024 / 1024);
	
	mat gpu_dists = dists;
	safe_call(cudaMalloc((void **) &gpu_dists.mat, sizeof(float) * dists.n));
	std::printf("Allocating: %dMB\n", sizeof(float) * dists.n / 1024 / 1024);

	//allocating the input buffer
	int* gpu_starting_inputid;
	safe_call(cudaMalloc((void **) &gpu_starting_inputid, sizeof(int) * (residual.n + 1)));
	safe_call(cudaMemcpy(gpu_starting_inputid, starting_inputid, sizeof(int) * (residual.n + 1), cudaMemcpyHostToDevice));
	
	Img* gpu_input;
	std::printf("Number of images: %d\n", num_imgs);
	std::printf("Allocating: %dMB\n", sizeof(Img) * num_imgs / 1024 / 1024);
	safe_call(cudaMalloc((void **) &gpu_input, sizeof(Img) * num_imgs));

	dim3 block(1024, 1, 1);
	dim3 grid(residual.n, 1, 1);
	
	//find biggest ivf entry
	int biggest = 0;
	for (int i = 0; i < ivf_size; i++ ) if (ivf[i].idstam > biggest) biggest = ivf[i].idstam; 

	int sm_size = 48 << 10;
	
	compute_dists<<<grid, block,  sm_size>>>(gpu_PQ, gpu_residual, gpu_ivf, gpu_entry_map, gpu_starting_imgid, gpu_starting_inputid, gpu_input, gpu_idxs, gpu_dists);  
	
	err = cudaGetLastError();

	if (err != cudaSuccess) {
		fprintf(stderr, "Failed to launch compute_dists kernel (error code %s)!\n",
				cudaGetErrorString(err));
		exit(EXIT_FAILURE);
	} else std::printf("SUCESSS!\n");
	
	std::printf("After calling the kernel\n");
	
	//RECEIVING DATA FROM GPU
	//safe_call(cudaMemcpy(elements, 0, sizeof(query_id_t) * residual.n, cudaMemcpyDeviceToHost));
	
	safe_call(cudaMemcpy(idxs.mat, gpu_idxs.mat , sizeof(int) * idxs.n, cudaMemcpyDeviceToHost));
	safe_call(cudaMemcpy(dists.mat, gpu_dists.mat, sizeof(float) * dists.n, cudaMemcpyDeviceToHost));
	
	//FREEING MEMORY
	cudaFree(gpu_PQ.centroids);
	cudaFree(gpu_residual.mat);
	cudaFree(gpu_ivf);
	
	for (int i = 0; i < ivf_size; i++) {
		cudaFree(tmp_ivf[i].ids);
		cudaFree(tmp_ivf[i].codes.mat);
	}
	
	cudaFree(gpu_entry_map);
	cudaFree(gpu_starting_imgid);
	cudaFree(gpu_idxs.mat);
	cudaFree(gpu_dists.mat);
	cudaFree(gpu_starting_inputid);
	cudaFree(gpu_input);
	
	delete[] tmp_ivf;

}

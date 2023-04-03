#include <stdlib.h>
#ifdef __cplusplus
#include <cuda_runtime.h>
#else
#include <cuda_runtime_api.h>
#endif
#include "cuda_gemv.h"

int ext_mpi_gemv_init(int row, int col, struct gemv_var *var) {
  float *x, alpha = 1e0, beta = 0e0;
  int i;
  var->row = row;
  var->col = col;
  cublasCreate(&var->handle);
  cudaMalloc((void**)&var->d_x, col * sizeof(float));
  cudaMalloc((void**)&var->d_alpha, sizeof(float));
  cudaMalloc((void**)&var->d_beta, sizeof(float));
  x = (float *)malloc(var->col * sizeof(float));
  for (i = 0; i < var->col; i++) {
    x[i] = 1e0;
  }
  cudaMemcpy(var->d_x, x, sizeof(float) * col, cudaMemcpyHostToDevice);   //copy x to device d_x
  cudaMemcpy(var->d_alpha, &alpha, sizeof(float), cudaMemcpyHostToDevice);
  cudaMemcpy(var->d_beta, &beta, sizeof(float), cudaMemcpyHostToDevice);
  free(x);
  return 0;
}

int ext_mpi_gemv_exec(struct gemv_var *var, void *d_A, void *d_y) {
  cublasSgemv(var->handle, CUBLAS_OP_T, var->col, var->row, var->d_alpha, d_A, var->col, var->d_x, 1, var->d_beta, d_y, 1); //swap col and row
  return 0;
}

int ext_mpi_gemv_done(struct gemv_var *var) {
  cudaFree(var->d_beta);
  cudaFree(var->d_alpha);
  cudaFree(var->d_x);
  cublasDestroy(var->handle);
  return 0;
}

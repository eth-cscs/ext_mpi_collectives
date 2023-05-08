#include <stdlib.h>
#ifdef __cplusplus
#include <cuda_runtime.h>
#else
#include <cuda_runtime_api.h>
#endif
#include "cuda_gemv.h"
#include "constants.h"

static double alphad = 1e0;
static double betad = 0e0;

int ext_mpi_gemv_init(char opcode, int row, int col, struct gemv_var *var) {
  double *xd, alphad = 1e0, betad = 0e0;
  float *xf, alphaf = 1e0, betaf = 0e0;
  int i;
  var->row = row;
  var->col = col;
  cublasCreate(&var->handle);
  switch (opcode) {
    case OPCODE_REDUCE_SUM_DOUBLE:
      cudaMalloc((void**)&var->d_x, col * sizeof(double));
      cudaMalloc((void**)&var->d_alpha, sizeof(double));
      cudaMalloc((void**)&var->d_beta, sizeof(double));
      xd = (double *)malloc(var->col * sizeof(double));
      for (i = 0; i < var->col; i++) {
        xd[i] = 1e0;
      }
      cudaMemcpy(var->d_x, xd, sizeof(double) * col, cudaMemcpyHostToDevice);   //copy x to device d_x
      cudaMemcpy(var->d_alpha, &alphad, sizeof(double), cudaMemcpyHostToDevice);
      cudaMemcpy(var->d_beta, &betad, sizeof(double), cudaMemcpyHostToDevice);
      free(xd);
      break;
    case OPCODE_REDUCE_SUM_FLOAT:
      cudaMalloc((void**)&var->d_x, col * sizeof(float));
      cudaMalloc((void**)&var->d_alpha, sizeof(float));
      cudaMalloc((void**)&var->d_beta, sizeof(float));
      xf = (float *)malloc(var->col * sizeof(float));
      for (i = 0; i < var->col; i++) {
        xf[i] = 1e0;
      }
      cudaMemcpy(var->d_x, xf, sizeof(float) * col, cudaMemcpyHostToDevice);   //copy x to device d_x
      cudaMemcpy(var->d_alpha, &alphaf, sizeof(float), cudaMemcpyHostToDevice);
      cudaMemcpy(var->d_beta, &betaf, sizeof(float), cudaMemcpyHostToDevice);
      free(xf);
      break;
    default:
      var->d_x = NULL;
      var->d_alpha = NULL;
      var->d_beta = NULL;
  }
  return 0;
}

int ext_mpi_gemv_exec(struct gemv_var *var, char opcode, void *d_a, int row, int col) {
  switch (opcode) {
    case OPCODE_REDUCE_SUM_DOUBLE:
      cublasDgemv(var->handle, CUBLAS_OP_N, row / sizeof(double), col, &alphad, d_a + row, row / sizeof(double), var->d_x, 1, &betad, d_a, 1);
      break;
    case OPCODE_REDUCE_SUM_FLOAT:
      cublasSgemv(var->handle, CUBLAS_OP_N, row / sizeof(float), col, var->d_alpha, d_a + row, row / sizeof(double), var->d_x, 1, var->d_beta, d_a, 1);
      break;
  }
  return 0;
}

int ext_mpi_gemv_done(struct gemv_var *var) {
  cudaFree(var->d_beta);
  cudaFree(var->d_alpha);
  cudaFree(var->d_x);
  cublasDestroy(var->handle);
  return 0;
}

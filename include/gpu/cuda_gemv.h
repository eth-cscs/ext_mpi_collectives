#ifndef CUDA_GEMV_H_

#define CUDA_GEMV_H_

#include <cublas_v2.h>

struct gemv_var {
  cublasHandle_t handle;
  int row;
  int col;
  void *d_x;
  void *d_alpha;
  void *d_beta;
};

#ifdef __cplusplus
extern "C"
{
#endif

int ext_mpi_gemv_init(char opcode, int row, int col, struct gemv_var *var);
int ext_mpi_gemv_exec(struct gemv_var *var, char opcode, void *d_a, int row, int col);
int ext_mpi_gemv_done(struct gemv_var *var);

#ifdef __cplusplus
}
#endif

#endif

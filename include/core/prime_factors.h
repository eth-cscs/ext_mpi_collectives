#ifndef EXT_MPI_PRIME_FACTORS_H_

#define EXT_MPI_PRIME_FACTORS_H_

#ifdef __cplusplus
extern "C"
{
#endif

int ext_mpi_factors_minimum(int number, int factor_min, int *factors);
int ext_mpi_factor_sqrt(int number);

#ifdef __cplusplus
}
#endif

#endif

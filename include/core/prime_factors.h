#ifndef EXT_MPI_PRIME_FACTORS_H_

#define EXT_MPI_PRIME_FACTORS_H_

struct prime_factors {
  int prime;
  int count;
};

#ifdef __cplusplus
extern "C"
{
#endif

int ext_mpi_prime_factor_decomposition(int number, struct prime_factors *factors);
int ext_mpi_plain_prime_factors(int number, int *prime_factors);
int ext_mpi_prime_factor_padding(int num, int *numbers);
int ext_mpi_factors_minimum(int number, int factor_min, int *factors);
int ext_mpi_factor_sqrt(int number);
int ext_mpi_greatest_common_divisor(int divisor1, int divisor2);

#ifdef __cplusplus
}
#endif

#endif

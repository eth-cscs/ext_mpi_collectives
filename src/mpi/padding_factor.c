#include <string.h>
#include <stdlib.h>
#include <mpi.h>
#include "prime_factors.h"

int ext_mpi_padding_factor(int number, MPI_Comm comm){
  struct prime_factors *factors;
  int max_factor, ret = 1, i, j;
  i = ext_mpi_prime_factor_decomposition(-number, NULL);
  PMPI_Allreduce(&i, &max_factor, 1, MPI_INT, MPI_MAX, comm);
  factors = (struct prime_factors *)malloc(sizeof(struct prime_factors) * (max_factor + 4));
  ext_mpi_prime_factor_decomposition(number, factors);
  memset(factors + i, 0, (max_factor - i) * sizeof(struct prime_factors));
  PMPI_Allreduce(MPI_IN_PLACE, factors, max_factor * sizeof(struct prime_factors) / sizeof(int), MPI_INT, MPI_MIN, comm);
  for (i = 0; i < max_factor; i++) {
    for (j = 0; j < factors[i].count; j++) {
      ret *= factors[i].prime;
    }
  }
  free(factors);
  return ret;
}

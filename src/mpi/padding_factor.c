#include <string.h>
#include <mpi.h>
#include "prime_factors.h"

int ext_mpi_padding_factor(int number, MPI_Comm comm){
  struct prime_factors factors[number];
  int max_factor, ret = 1, i, j;
  i = ext_mpi_prime_factor_decomposition(number, factors);
  PMPI_Allreduce(&i, &max_factor, 1, MPI_INT, MPI_MAX, comm);
  memset(factors + i, 0, (max_factor - i) * sizeof(struct prime_factors));
  PMPI_Allreduce(MPI_IN_PLACE, factors, max_factor * sizeof(struct prime_factors), MPI_INT, MPI_MIN, comm);
  for (i = 0; i < max_factor; i++) {
    for (j = 0; j < factors[i].count; j++) {
      ret *= factors[i].prime;
    }
  }
  return ret;
}

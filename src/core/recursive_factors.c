#include "recursive_factors.h"
#include "prime_factors.h"
#include <stdlib.h>

#define FACTOR_MAX 13

static int compare(const void *a, const void *b) {
  return *(int*)a - *(int*)b;
}

int ext_mpi_heuristic_recursive_factors(int num_nodes, int **factors_max, int ***factors) {
  int plain_factors_max, plain_factors[num_nodes], temp_factors_max, temp_factors[num_nodes], i, j, k;
  plain_factors_max = ext_mpi_plain_prime_factors(num_nodes, plain_factors);
  temp_factors_max = plain_factors_max;
  for (i = 0; i < plain_factors_max; i++) {
    temp_factors[i] = plain_factors[i];
  }
  *factors_max = (int *)malloc(num_nodes*num_nodes*FACTOR_MAX*sizeof(int));
  *factors = (int **)malloc(num_nodes*num_nodes*FACTOR_MAX*sizeof(int *));
  i = 0;
  while (1) {
    if (temp_factors[temp_factors_max - 1] > FACTOR_MAX)
      return i;
    (*factors_max)[i] = temp_factors_max * 2;
    (*factors)[i] = (int*)malloc(((*factors_max)[i] + 1) * sizeof(int));
    for (j = 0; j < temp_factors_max; j++) {
      (*factors)[i][j + temp_factors_max] = temp_factors[j];
      (*factors)[i][temp_factors_max - 1 - j] = -temp_factors[j];
    }
    (*factors)[i][2 * temp_factors_max] = 0;
    for (k = 1; k < temp_factors_max + 1; k++) {
      (*factors_max)[i + k] = temp_factors_max * 2 - k;
      (*factors)[i + k] = (int*)malloc(((*factors_max)[i] + 1) * sizeof(int));
      for (j = k; (*factors)[i][j]; j++) {
	(*factors)[i + k][j - k] = (*factors)[i][j];
      }
      for (j = 0; j < temp_factors_max - k; j++) {
	(*factors)[i + k][j] = -(*factors)[i][2 * temp_factors_max - j - 1];
      }
      (*factors)[i + k][2 * temp_factors_max - k] = 0;
    }
    i += k;
    temp_factors[0] *= temp_factors[1];
    temp_factors_max--;
    for (j = 1; j < temp_factors_max; j++) {
      temp_factors[j] = temp_factors[j + 1];
    }
    qsort(temp_factors, temp_factors_max, sizeof(int), compare);
  }
}

#include "recursive_factors.h"
#include "prime_factors.h"
#include "read_bench.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#define FACTOR_MAX 13

static int compare(const void *a, const void *b) {
  return -*(int*)a + *(int*)b;
}

int ext_mpi_heuristic_recursive_factors(int num_nodes, int **factors_max, int ***factors) {
  int plain_factors_max, plain_factors[num_nodes], temp_factors_max, temp_factors[num_nodes], i, j, k;
  plain_factors_max = ext_mpi_plain_prime_factors(num_nodes, plain_factors);
  temp_factors_max = plain_factors_max;
  for (i = 0; i < plain_factors_max; i++) {
    temp_factors[i] = plain_factors[i];
  }
  qsort(temp_factors, temp_factors_max, sizeof(int), compare);
  *factors_max = (int *)malloc(num_nodes*num_nodes*FACTOR_MAX*sizeof(int));
  memset(*factors_max, 0, num_nodes*num_nodes*FACTOR_MAX*sizeof(int));
  *factors = (int **)malloc(num_nodes*num_nodes*FACTOR_MAX*sizeof(int *));
  memset(*factors, 0, num_nodes*num_nodes*FACTOR_MAX*sizeof(int *));
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

static double cost_single(int msize, int nports) {
  double mb;
  int i;
  mb = msize;
  mb /= ext_mpi_file_input[(nports - 1) * ext_mpi_file_input_max_per_core].parallel;
  i = floor(mb / (ext_mpi_file_input[1].msize - ext_mpi_file_input[0].msize)) - 1;
  if (i < 0) {
    return ext_mpi_file_input[0 + (nports - 1) * ext_mpi_file_input_max_per_core].deltaT / nports;
  } else if (i >= ext_mpi_file_input_max_per_core) {
    return (ext_mpi_file_input[ext_mpi_file_input_max_per_core - 1 +
                        (nports - 1) * ext_mpi_file_input_max_per_core].deltaT *
             mb /
             ext_mpi_file_input[ext_mpi_file_input_max_per_core - 1 +
                        (nports - 1) * ext_mpi_file_input_max_per_core].msize) / nports;
  } else {
    return
        (ext_mpi_file_input[i + (nports - 1) * ext_mpi_file_input_max_per_core].deltaT +
        (mb - ext_mpi_file_input[i + (nports - 1) * ext_mpi_file_input_max_per_core].msize) *
            (ext_mpi_file_input[i + 1 + (nports - 1) * ext_mpi_file_input_max_per_core].deltaT -
             ext_mpi_file_input[i + (nports - 1) * ext_mpi_file_input_max_per_core].deltaT) /
            (ext_mpi_file_input[i + 1 + (nports - 1) * ext_mpi_file_input_max_per_core].msize -
             ext_mpi_file_input[i + (nports - 1) * ext_mpi_file_input_max_per_core].msize)) / nports * nports;
  }
}

static double cost_minimal(int msize, int *nports) {
  double T, T_min = 1e99;
  int i;
  for (i = 0; i < FACTOR_MAX - 1; i++) {
    T = cost_single(msize, i + 1);
    if (T < T_min) {
      T_min = T;
      *nports = i + 1;
    }
  }
  return T_min;
}

double ext_mpi_min_cost_total(int msize, int num, int *factors_max, int **factors, int *ind_min) {
  double T, T_min = 1e99, m;
  int i, j, i_min = -1;
  for (i = 0; i < num; i++) {
    T = 0e0;
    m = msize;
    for (j = 0; j < factors_max[i]; j++) {
      if (factors[i][j] < 0) {
        m /= abs(factors[i][j]);
        if (m <= 0) {
          m = 1;
        }
      }
      if (abs(factors[i][j]) <= FACTOR_MAX) {
      T += cost_single(m, abs(factors[i][j]) - 1);
      } else {
        T += 1e99;
      }
      if (factors[i][j] > 0) {
        m *= factors[i][j];
      }
    }
//printf("%e\n", T);
    if (T < T_min) {
      T_min = T;
      i_min = i;
    }
  }
  *ind_min = i_min;
  return T_min;
}

int ext_mpi_heuristic_recursive_non_factors(int num_nodes, int **factors_max, int ***factors) {
  int factors_max_max = 0, i, j;
  *factors_max = (int *)malloc(num_nodes*num_nodes*FACTOR_MAX*sizeof(int));
  memset(*factors_max, 0, num_nodes*num_nodes*FACTOR_MAX*sizeof(int));
  *factors = (int **)malloc(num_nodes*num_nodes*FACTOR_MAX*sizeof(int *));
  memset(*factors, 0, num_nodes*num_nodes*FACTOR_MAX*sizeof(int *));
  for (i = 1; i < num_nodes*num_nodes*FACTOR_MAX; i++) {
    (*factors)[i] = (int *)malloc(num_nodes*sizeof(int));
    (*factors_max)[i] = 0;
    for (j = 0; j < num_nodes; j++) {
      (*factors)[i][j] = 2;
      (*factors_max)[i]++;
    }
    factors_max_max++;
  }
  return factors_max_max;
}

int main__() {
  double d;
  int factors_max_max, *factors_max, **factors, i, j;
  ext_mpi_read_bench();
  factors_max_max = ext_mpi_heuristic_recursive_non_factors(32, &factors_max, &factors);
//  d = cost_minimal(100000, &i);
//  printf("aaaaa %d %e\n", i, d);
  for (i = 0; i < factors_max_max; i++) {
    for (j = 0; j < factors_max[i]; j++) {
      printf("%d ", factors[i][j]);
    }
    printf("\n");
  }
  d = ext_mpi_min_cost_total(100000, factors_max_max, factors_max, factors, &i);
  printf("aaaaa %d %e\n", i, d);
  return 0;
}
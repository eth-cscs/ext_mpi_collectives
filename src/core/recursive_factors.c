#include "recursive_factors.h"
#include "prime_factors.h"
#include "read_bench.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

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

static double cost_single(double msize, int nports) {
  double mb;
  int i;
  mb = msize;
  mb /= ext_mpi_file_input[(nports - 1) * ext_mpi_file_input_max_per_core].parallel;
  i = floor(mb / (ext_mpi_file_input[1].msize - ext_mpi_file_input[0].msize)) - 1;
  if (i < 0) {
    return ext_mpi_file_input[0 + (nports - 1) * ext_mpi_file_input_max_per_core].deltaT / nports * nports;
  } else if (i >= ext_mpi_file_input_max_per_core) {
    return (ext_mpi_file_input[ext_mpi_file_input_max_per_core - 1 +
                        (nports - 1) * ext_mpi_file_input_max_per_core].deltaT *
             mb /
             ext_mpi_file_input[ext_mpi_file_input_max_per_core - 1 +
                        (nports - 1) * ext_mpi_file_input_max_per_core].msize) / nports * nports;
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

/*static double cost_minimal(int msize, int *nports) {
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
}*/

double ext_mpi_min_cost_total(int msize, int num, int *factors_max, int **factors, int *primes, int *ind_min) {
  double T, T_min = 1e99, m;
  int i, j, i_min = -1, max_minus;
  for (i = 0; i < num; i++) {
    T = 0e0;
    m = msize;
    for (max_minus = 0; max_minus < factors_max[i] && factors[i][max_minus] < 0; max_minus++)
      ;
    if (!primes[i]) {
      max_minus = factors_max[i];
      T += m * 1e-8*10;
    }
    for (j = 0; j < factors_max[i]; j++) {
      if (factors[i][j] < 0) {
        m /= abs(factors[i][j]);
        if (m <= 0e0) {
          m = 1e0;
        }
      }
      if (abs(factors[i][j]) <= FACTOR_MAX) {
        T += cost_single(m, abs(factors[i][j]) - 1);
      } else {
        T += 1e99;
      }
      if (factors[i][j] > 0 && j >= factors_max[i] - max_minus) {
        m *= factors[i][j];
      }
    }
    if (T < T_min) {
      T_min = T;
      i_min = i;
    }
  }
  *ind_min = i_min;
  return T_min;
}

static int next_number(int num_nodes, int *numbers) {
  int ret = 1, flag, flag2 = 1, i, j, k, l, m = num_nodes + 1;
  for (j = 0, k = 1; k < num_nodes; k *= 2, j++);
  while (flag2 && ret) {
    flag = 1; flag2 = 0;
    i = 0;
    while (flag && ret) {
      flag = 0;
      if (i < j) {
        if (j - 1 - i < m) {
          m = j - 1 - i;
        }
        numbers[j - 1 - i]++;
        if (numbers[j - 1 - i] > FACTOR_MAX) {
          numbers[j - 1 - i] = 2;
          flag = 1;
        }
        i++;
      } else {
        ret = 0;
      }
    }
    for (l = 0, k = 1; k < num_nodes; k *= numbers[l], l++);
    k /= numbers[l - 1];
    if ((m > l - 1) || (k * (numbers[l - 1] - 1) >= num_nodes)) {
      flag2 = 1;
    }
  }
  return ret;
}

static int correct_number(int num_nodes, int *numbers, int *numbers_new) {
  int i, k;
  for (i = 0, k = 1; k < num_nodes; k *= numbers[i], i++) {
    numbers_new[i] = numbers[i];
  }
  i--;
  k /= numbers_new[i];
  numbers_new[i] = 2;
  while (numbers_new[i] * k < num_nodes) {
    numbers_new[i]++;
  }
  return numbers_new[i] != numbers[i];
}

int ext_mpi_heuristic_recursive_non_factors(int num_nodes, int allgather, int **factors_max, int ***factors, int **primes) {
  int factors_max_max = 0, numbers[num_nodes], numbers_corrected[num_nodes], numbers_max, flag = 1, lines_max, i, j, k, l;
  lines_max = num_nodes * num_nodes * FACTOR_MAX * FACTOR_MAX;
  for (i = 0; i < num_nodes; i++){
    numbers[i] = 2;
  }
  correct_number(num_nodes, numbers, numbers_corrected);
  *factors_max = (int *)malloc(lines_max * sizeof(int));
  memset(*factors_max, 0, lines_max * sizeof(int));
  *factors = (int **)malloc(lines_max * sizeof(int *));
  memset(*factors, 0, lines_max * sizeof(int *));
  *primes = (int *)malloc(lines_max * sizeof(int));
  memset(*primes, 0, lines_max * sizeof(int));
  for (i = 0; i < lines_max && flag; i += l) {
    for (numbers_max = 0, k = 1; k < num_nodes; k *= numbers_corrected[numbers_max], numbers_max++)
      ;
    for (l = 0; l < (allgather ? 1 : numbers_max + 1); l++) {
      (*primes)[i + l] = k == num_nodes;
      (*factors)[i + l] = (int *)malloc(num_nodes * 2 * sizeof(int));
      (*factors_max)[i + l] = 0;
      for (j = 0; j < numbers_max; j++) {
        (*factors)[i + l][j + l] = numbers_corrected[j];
      }
      for (j = 0; j < l; j++) {
        (*factors)[i + l][j] = -numbers_corrected[numbers_max - 1 - j];
      }
      (*factors_max)[i + l] += numbers_max + l;
    }
    flag = next_number(num_nodes, numbers_corrected);
    factors_max_max += l;
  }
  return factors_max_max;
}

int ext_mpi_heuristic_cancel_factors(int factors_max_max, int *factors_max, int **factors, int *primes) {
  int cancel, i, j, k, l;
  for (k = 0; k < factors_max_max; k++) {
    cancel = 0;
    l = INT_MAX;
    for (j = 0; j < factors_max[k]; j++) {
      if ((factors[k][j] > 0) && (factors[k][j] <= l)) {
        l = factors[k][j];
      } else if (factors[k][j] > 0) {
        cancel = 1;
      }
    }
    if (cancel) {
      free(factors[k]);
      factors_max_max--;
      for (i = k; i < factors_max_max; i++) {
        factors_max[i] = factors_max[i + 1];
        factors[i] = factors[i + 1];
        primes[i] = primes[i + 1];
      }
      k--;
    }
  }
  return factors_max_max;
}

/*int main() {
  double d;
  int factors_max_max, *factors_max, **factors, *primes, i, j;
  ext_mpi_read_bench();
  factors_max_max = ext_mpi_heuristic_recursive_non_factors(32, 0, &factors_max, &factors, &primes);
  factors_max_max = ext_mpi_heuristic_cancel_factors(factors_max_max, factors_max, factors, primes);
  for (i = 0; i < factors_max_max; i++) {
    printf("%d: prime %d : ", i, primes[i]);
    for (j = 0; j < factors_max[i]; j++) {
      printf("%d ", factors[i][j]);
    }
    printf("\n");
  }
  d = ext_mpi_min_cost_total(2048, factors_max_max, factors_max, factors, primes, &i);
  printf("aaaaa %d %e\n", i, d);
  return 0;
}*/

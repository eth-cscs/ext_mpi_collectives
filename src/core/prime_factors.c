#include "prime_factors.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

static void prime_rost(int max_number, int *primes) {
  int i, j;
  for (i = 0; i < max_number; i++) {
    primes[i] = 1;
  }
  for (i = 2; i < max_number; i++) {
    if (primes[i]) {
      for (j = 2; i * j < max_number; j++) {
        primes[i * j] = 0;
      }
    }
  }
}

int ext_mpi_prime_factor_decomposition(int number,
                                      struct prime_factors *factors) {
  int *primes, max_factor = 0, i, j;
  primes = (int*)malloc((abs(number) + 2) * sizeof(int));
  if (number < 0) {
    prime_rost(abs(number) + 1, primes);
    for (i = 2; i < abs(number) + 1; i++) {
      if (primes[i]) {
        max_factor++;
      }
    }
    free(primes);
    return max_factor;
  }
  prime_rost(number + 1, primes);
  for (i = 2; i < number + 1; i++) {
    if (primes[i]) {
      factors[max_factor++].prime = i;
    }
  }
  for (i = 0; i < max_factor; i++) {
    factors[i].count = 0;
  }
  for (i = 0; i < max_factor; i++) {
    j = number;
    while (j % factors[i].prime == 0) {
      j /= factors[i].prime;
      factors[i].count++;
    }
  }
  free(primes);
  return max_factor;
}

int ext_mpi_plain_prime_factors(int number, int *prime_factors) {
  struct prime_factors factors[number + 1];
  int primes_max, i, j, k;
  primes_max = ext_mpi_prime_factor_decomposition(number, factors);
  for (i = k = 0; i < primes_max; i++) {
    for (j = 0; j < factors[i].count; j++) {
      prime_factors[k++] = factors[i].prime;
    }
  }
  prime_factors[k] = 0;
  return k;
}

static int prime_factor_decomposition_array(int num, int *numbers, int *factors_prime, int *factors_count) {
  int *primes, max_factor = 0, i, j, k, max_number = 0;
  for (i = 0; i < abs(num); i++) {
    if (numbers[i] > max_number) {
      max_number = numbers[i];
    }
  }
  primes = (int *)malloc((max_number + 2) * sizeof(int));
  prime_rost(max_number + 1, primes);
  for (i = 2; i < max_number + 1; i++) {
    if (primes[i]) {
      factors_prime[max_factor++] = i;
    }
  }
  if (num > 0) {
    for (i = 0; i < max_factor; i++) {
      for (k = 0; k < num; k++) {
        factors_count[i * num + k] = 0;
        j = numbers[k];
        while (j % factors_prime[i] == 0) {
          j /= factors_prime[i];
          factors_count[i * num + k]++;
	}
      }
    }
  }
  free(primes);
  return max_factor;
}

int ext_mpi_prime_factor_padding(int num, int *numbers) {
  int *factors_prime, *factors_count, max_number = 0, i, j, ret = 1, max_factor;
  if (num == 0) {
    return 1;
  }
  for (i = 0; i < num; i++) {
    if (numbers[i] > max_number) {
      max_number = numbers[i];
    }
    if (numbers[i] <= 0) {
      printf("negative or zero factor in prime factor decomposition\n");
      exit(1);
    }
  }
  factors_prime = (int *)malloc((max_number + 2) * sizeof(int));
  max_factor = prime_factor_decomposition_array(-num, numbers, factors_prime, NULL);
  free(factors_prime);
  factors_prime = (int *)malloc((max_factor + 2) * sizeof(int));
  factors_count = (int *)malloc((max_factor + 2) * num * sizeof(int));
  max_factor = prime_factor_decomposition_array(num, numbers, factors_prime, factors_count);
  for (i = 0; i < max_factor; i++) {
    for (j = 1; j < num; j++) {
      if (factors_count[i * num + j] < factors_count[i * num]) {
	factors_count[i * num] = factors_count[i * num + j];
      }
    }
    for (j = 0; j < factors_count[i * num]; j++) {
      ret *= factors_prime[i];
    }
  }
  free(factors_count);
  free(factors_prime);
  return ret;
}

static int factors_minimum_compare(const void *a, const void *b) {
  return (*(int *)a - *(int *)b);
}

static int factors_minimum_compare_reverse(const void *a, const void *b) {
  return (*(int *)b - *(int *)a);
}

int ext_mpi_factors_minimum(int number, int factor_min, int *factors) {
  int factors_max, i;
  factors_max = ext_mpi_plain_prime_factors(number, factors);
  while ((factors[0] < factor_min) && (factors_max >= 2)) {
    factors[0] *= factors[1];
    factors_max--;
    for (i = 1; i <= factors_max; i++) {
      factors[i] = factors[i + 1];
    }
    qsort(factors, factors_max, sizeof(int), factors_minimum_compare);
  }
  if (factors_max >= 2) {
    qsort(factors, factors_max, sizeof(int), factors_minimum_compare_reverse);
  }
  return factors_max;
}

int ext_mpi_factor_sqrt(int number) {
  int factors[number], factors_max, i;
  factors_max = ext_mpi_plain_prime_factors(number, factors);
  while ((factors[0] < sqrt(number)) && (factors_max >= 2)) {
    factors[0] *= factors[1];
    factors_max--;
    for (i = 1; i <= factors_max; i++) {
      factors[i] = factors[i + 1];
    }
  }
  return factors[0];
}

int ext_mpi_greatest_common_divisor(int divisor1, int divisor2) {
  int num_primes1, num_primes2, primes1[divisor1 + 1], primes2[divisor2 + 1], result, i, j;
  num_primes1 = ext_mpi_plain_prime_factors(divisor1, primes1);
  num_primes2 = ext_mpi_plain_prime_factors(divisor2, primes2);
  for (i = 0; i < num_primes1; i++) {
    for (j = 0; j < num_primes2; j++) {
      if (primes1[i] == primes2[j]) {
        primes1[i] = 1;
        primes2[j] = 1;
      }
    }
  }
  result = divisor2;
  for (i = 0; i < num_primes1; i++) {
    result *= primes1[i];
  }
  return result;
}

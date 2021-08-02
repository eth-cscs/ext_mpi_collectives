#include "ext_mpi_alltoall.h"
#include "ext_mpi_alltoall_native.h"
#include <math.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BREAK_SIZE 25000
#define MERGE_SIZE 250

typedef struct _FileData {
  int nnodes;
  int nports;
  int parallel;
  int msize;
  double deltaT;
} FileData;

static FileData *file_input;
static int file_input_max, file_input_max_per_core;
static int is_initialised = 0;
int enforce_bruck = 0;
int copyin_method = 0;
int threshold_rabenseifner = 10000;
int *fixed_factors = NULL;
int *fixed_factors_limit = NULL;
int fixed_tile_size_row = 0;
int fixed_tile_size_column = 0;

static void read_env() {
  int mpi_comm_rank, mpi_comm_size, var, i, j;
  char *c;
  MPI_Comm_rank(MPI_COMM_WORLD, &mpi_comm_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &mpi_comm_size);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_FIXED_TILE_SIZE_ROW")) != NULL);
    if (var) {
      sscanf(c, "%d", &fixed_tile_size_row);
      printf("fixed tile size row %d\n", fixed_tile_size_row);
    }
  }
  MPI_Bcast(&fixed_tile_size_row, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_FIXED_TILE_SIZE_COLUMN")) != NULL);
    if (var) {
      sscanf(c, "%d", &fixed_tile_size_column);
      printf("fixed tile size column %d\n", fixed_tile_size_column);
    }
  }
  MPI_Bcast(&fixed_tile_size_column, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_THRESHOLD_RABENSEIFNER")) != NULL);
    if (var) {
      sscanf(c, "%d", &threshold_rabenseifner);
      printf("threshold Rabenseifer %d\n", threshold_rabenseifner);
    }
  }
  MPI_Bcast(&threshold_rabenseifner, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_ENFORCE_BRUCK")) != NULL);
    if (var) {
      if ((c[0] != '0') && (c[0] != 'F')) {
        enforce_bruck = 1;
        printf("Bruck enforced\n");
      }
    }
  }
  MPI_Bcast(&enforce_bruck, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_COPYIN_METHOD")) != NULL);
    if (var) {
      if (c[0] == '1') {
        copyin_method = 1;
        printf("copy in short\n");
      }
      if (c[0] == '2') {
        copyin_method = 2;
        printf("copy in long\n");
      }
    }
  }
  MPI_Bcast(&copyin_method, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_FIXED_FACTORS")) != NULL);
  }
  MPI_Bcast(&var, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (var && (fixed_factors != NULL)) {
    free(fixed_factors);
  }
  if (var) {
    i = 1;
    j = 2;
    while (i < mpi_comm_size) {
      j++;
      i *= 2;
    }
    fixed_factors = (int *)malloc(j * sizeof(int));
    if (mpi_comm_rank == 0) {
      i = 0;
      j = 0;
      while (c[i] != 0) {
        sscanf(c, "%d", &fixed_factors[j]);
        j++;
        while ((c[i] != 0) && (c[i] != ',')) {
          c++;
        }
        if (c[i] == ',') {
          c++;
        }
      }
    }
    fixed_factors[j++] = 0;
    MPI_Bcast(&j, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(fixed_factors, j, MPI_INT, 0, MPI_COMM_WORLD);
  }
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_FIXED_FACTORS_LIMIT")) != NULL);
  }
  MPI_Bcast(&var, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (var && (fixed_factors_limit != NULL)) {
    free(fixed_factors_limit);
  }
  if (var) {
    i = 1;
    j = 2;
    while (i < mpi_comm_size) {
      j++;
      i *= 2;
    }
    fixed_factors_limit = (int *)malloc(j * sizeof(int));
    if (mpi_comm_rank == 0) {
      i = 0;
      j = 0;
      while (c[i] != 0) {
        sscanf(c, "%d", &fixed_factors_limit[j]);
        j++;
        while ((c[i] != 0) && (c[i] != ',')) {
          c++;
        }
        if (c[i] == ',') {
          c++;
        }
      }
    }
    fixed_factors_limit[j++] = 0;
    MPI_Bcast(&j, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(fixed_factors_limit, j, MPI_INT, 0, MPI_COMM_WORLD);
  }
}

static void read_bench() {
  FileData *file_input_raw;
  char lstring[10000];
  double distance;
  int file_input_max_raw, file_input_max_per_core_raw;
  int cores, i, j, mpi_comm_rank, mpi_comm_size, fallback;
  file_input_max_raw = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &mpi_comm_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &mpi_comm_size);
  fallback = 0;
  MPI_Bcast(&fallback, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (!fallback) {
    if (mpi_comm_rank == 0) {
#include "latency_bandwidth.tmp"
      file_input_max_per_core_raw =
          file_input_max_raw / file_input_raw[file_input_max_raw - 1].nports;
      distance = file_input_raw[1].msize - file_input_raw[0].msize;
      file_input_max_per_core = (file_input_raw[file_input_max_raw - 1].msize -
                                 file_input_raw[0].msize) /
                                distance;
      file_input_max = file_input_max_per_core *
                       file_input_raw[file_input_max_raw - 1].nports;
      file_input = (FileData *)malloc(file_input_max * sizeof(*file_input));
      for (cores = 1; cores <= file_input_raw[file_input_max_raw - 1].nports;
           cores++) {
        j = 0;
        for (i = 0; i < file_input_max_per_core; i++) {
          for (; file_input_raw[j].msize <
                     i * distance + file_input_raw[0].msize &&
                 j < file_input_max_per_core_raw;
               j++) {
          }
          if (j == file_input_max_per_core_raw) {
            file_input[i + file_input_max_per_core * (cores - 1)] =
                file_input_raw[j + file_input_max_per_core_raw * (cores - 1)];
          } else {
            if (j == 0) {
              file_input[i + file_input_max_per_core * (cores - 1)] =
                  file_input_raw[j + file_input_max_per_core_raw * (cores - 1)];
            } else {
              file_input[i + file_input_max_per_core * (cores - 1)].nnodes =
                  file_input_raw[j + file_input_max_per_core_raw * (cores - 1)]
                      .nnodes;
              file_input[i + file_input_max_per_core * (cores - 1)].nports =
                  file_input_raw[j + file_input_max_per_core_raw * (cores - 1)]
                      .nports;
              file_input[i + file_input_max_per_core * (cores - 1)].parallel =
                  file_input_raw[j + file_input_max_per_core_raw * (cores - 1)]
                      .parallel;
              file_input[i + file_input_max_per_core * (cores - 1)].msize =
                  i * distance + file_input_raw[0].msize;
              file_input[i + file_input_max_per_core * (cores - 1)].deltaT =
                  file_input_raw[j - 1 +
                                 file_input_max_per_core_raw * (cores - 1)]
                      .deltaT +
                  (file_input[i + file_input_max_per_core * (cores - 1)].msize -
                   file_input_raw[j - 1 +
                                  file_input_max_per_core_raw * (cores - 1)]
                       .msize) *
                      (file_input_raw[j +
                                      file_input_max_per_core_raw * (cores - 1)]
                           .deltaT -
                       file_input_raw[j - 1 +
                                      file_input_max_per_core_raw * (cores - 1)]
                           .deltaT) /
                      (file_input_raw[j +
                                      file_input_max_per_core_raw * (cores - 1)]
                           .msize -
                       file_input_raw[j - 1 +
                                      file_input_max_per_core_raw * (cores - 1)]
                           .msize);
            }
          }
        }
      }
      free(file_input_raw);
      for (i = 0; i < file_input_max / file_input_max_per_core; i++) {
        for (j = file_input_max_per_core - 1 - 1; j >= 0; j--) {
          if (file_input[j + i * file_input_max_per_core].deltaT >
              file_input[(j + 1) + i * file_input_max_per_core].deltaT) {
            file_input[j + i * file_input_max_per_core].deltaT =
                file_input[(j + 1) + i * file_input_max_per_core].deltaT;
          }
        }
      }
    }
    MPI_Bcast(&file_input_max, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&file_input_max_per_core, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (mpi_comm_rank != 0) {
      file_input = (FileData *)malloc(file_input_max * sizeof(*file_input));
    }
    MPI_Bcast(file_input, file_input_max * sizeof(*file_input), MPI_CHAR, 0,
              MPI_COMM_WORLD);
  } else {
    i = 1;
    j = 2;
    while (i < mpi_comm_size) {
      j++;
      i *= 2;
    }
    fixed_factors = (int *)malloc((j + 1) * sizeof(int));
    for (i = 0; i < j; i++) {
      fixed_factors[i] = 2;
    }
    fixed_factors[j] = -1;
  }
}

static double cost_recursive(int p, double n, int fac, int port_max,
                             int *rarray) {
  double T, T_min, ma, mb;
  int r, i, j, k, tarray[p + 1];
  T_min = 1e60;
  if (port_max > file_input[file_input_max - 1].nports) {
    port_max = file_input[file_input_max - 1].nports;
  }
  if (port_max > (p - 1) / fac + 1) {
    port_max = (p - 1) / fac + 1;
  }
  if (port_max < 1) {
    port_max = 1;
  }
  for (i = 1; i <= port_max; i++) {
    r = i + 1;
    ma = fac;
    if (ma * r > p) {
      ma = (p - ma) / (r - 1);
    }
    mb = n * ma;
    mb /= file_input[(r - 1 - 1) * file_input_max_per_core].parallel;
    j = floor(mb / (file_input[1].msize - file_input[0].msize)) - 1;
    k = j + 1;
    if (j < 0) {
      T = file_input[0 + (r - 1 - 1) * file_input_max_per_core].deltaT;
    } else {
      if (k >= file_input_max_per_core) {
        T = file_input[file_input_max_per_core - 1 +
                       (r - 1 - 1) * file_input_max_per_core]
                .deltaT *
            mb /
            file_input[file_input_max_per_core - 1 +
                       (r - 1 - 1) * file_input_max_per_core]
                .msize;
      } else {
        T = file_input[j + (r - 1 - 1) * file_input_max_per_core].deltaT +
            (mb - file_input[j + (r - 1 - 1) * file_input_max_per_core].msize) *
                (file_input[k + (r - 1 - 1) * file_input_max_per_core].deltaT -
                 file_input[j + (r - 1 - 1) * file_input_max_per_core].deltaT) /
                (file_input[k + (r - 1 - 1) * file_input_max_per_core].msize -
                 file_input[j + (r - 1 - 1) * file_input_max_per_core].msize);
      }
    }
    if (fac * r < p) {
      T += cost_recursive(p, n, fac * r, port_max, tarray + 1);
    } else {
      tarray[1] = 0;
    }
    if (T < T_min) {
      T_min = T;
      tarray[0] = r;
      j = 0;
      while (tarray[j] > 0) {
        rarray[j] = tarray[j];
        j++;
      }
      rarray[j] = 0;
    }
  }
  return (T_min);
}

static void cost_explicit(int p, double n, int port_max, int *rarray) {
  double T, T_min, ma, mb;
  int r, i, j, k, l, rr;
  if (port_max > file_input[file_input_max - 1].nports) {
    port_max = file_input[file_input_max - 1].nports;
  }
  if (port_max < 1) {
    port_max = 1;
  }
  l = 0;
  ma = 1;
  rr = 1;
  while (rr < p) {
    T_min = 1e60;
    for (i = 1; i <= port_max; i++) {
      r = i + 1;
      mb = n * rr;
      mb /= file_input[(r - 1 - 1) * file_input_max_per_core].parallel;
      j = floor(mb / (file_input[1].msize - file_input[0].msize)) - 1;
      k = j + 1;
      if (j < 0) {
        T = file_input[0 + (r - 1 - 1) * file_input_max_per_core].deltaT;
      } else {
        if (k >= file_input_max_per_core) {
          T = file_input[file_input_max_per_core - 1 +
                         (r - 1 - 1) * file_input_max_per_core]
                  .deltaT *
              mb /
              file_input[file_input_max_per_core - 1 +
                         (r - 1 - 1) * file_input_max_per_core]
                  .msize;
        } else {
          T = file_input[j + (r - 1 - 1) * file_input_max_per_core].deltaT +
              (mb -
               file_input[j + (r - 1 - 1) * file_input_max_per_core].msize) *
                  (file_input[k + (r - 1 - 1) * file_input_max_per_core]
                       .deltaT -
                   file_input[j + (r - 1 - 1) * file_input_max_per_core]
                       .deltaT) /
                  (file_input[k + (r - 1 - 1) * file_input_max_per_core].msize -
                   file_input[j + (r - 1 - 1) * file_input_max_per_core].msize);
        }
        T /= r;
      }
      if (T < T_min) {
        T_min = T;
        rarray[l] = r;
      }
    }
    rr *= rarray[l];
    l++;
  }
}

struct prime_factors {
  int prime;
  int count;
};

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

static int prime_factor_decomposition(int number,
                                      struct prime_factors *factors) {
  int primes[number + 2], max_factor, i, j;
  for (i = 0; i < number + 1; i++) {
    factors[i].count = factors[i].prime = 0;
  }
  prime_rost(number + 1, primes);
  max_factor = 0;
  for (i = 2; i < number + 1; i++) {
    if (primes[i]) {
      factors[max_factor++].prime = i;
    }
  }
  for (i = 0; i < max_factor; i++) {
    j = number;
    while (j % factors[i].prime == 0) {
      j /= factors[i].prime;
      factors[i].count++;
    }
  }
  return (max_factor);
}

static void communication_factors_sub(int number, int max_factor, int mmm,
                                      int *factors, int *max_factors) {
  struct prime_factors primes[number];
  int max_primes, i, j, k, l, m, n, o;
  max_primes = prime_factor_decomposition(number, primes);
  j = 0;
  for (i = max_primes - 1; i >= 0; i--) {
    while (primes[i].count > 0) {
      primes[i].count--;
      if (primes[i].prime > max_factor) {
        k = ceil(log(primes[i].prime) / log(max_factor));
        l = exp(log(primes[i].prime) / k) + 1;
        for (m = 0; m < k; m++) {
          factors[j] = l;
          max_factors[j++] = primes[i].prime;
        }
        if (mmm) {
          m = 1;
          o = 1;
          while (m) {
            m = 0;
            for (n = i; n >= 0; n--) {
              if ((primes[n].count > 0) &&
                  (primes[n].prime * pow(primes[i].prime, k) <=
                   pow(max_factor, k))) {
                o *= primes[n].prime;
                primes[n].count--;
                m = 1;
                n = -1;
              }
            }
          }
          l = ceil(pow(pow(l, k) * o, 1. / k));
          for (m = 0; m < k; m++) {
            factors[j - m] = l;
          }
        }
      } else {
        factors[j] = primes[i].prime;
        m = 1;
        while (m) {
          m = 0;
          for (l = i; l >= 0; l--) {
            if ((primes[l].count > 0) &&
                (factors[j] * primes[l].prime <= max_factor)) {
              factors[j] *= primes[l].prime;
              primes[l].count--;
              m = 1;
              l = -1;
            }
          }
        }
        max_factors[j] = factors[j];
        j++;
      }
    }
  }
  factors[j] = max_factors[j] = 0;
}

static void communication_factors(int number, int max_factor, int *factors,
                                  int *max_factors) {
  int a, b, as, bs;
  if (number == 1) {
    factors[0] = 1;
  } else {
    communication_factors_sub(number, max_factor, 0, factors, max_factors);
    a = as = 0;
    while (factors[a] > 0) {
      a++;
      as += factors[a];
    }
    communication_factors_sub(number, max_factor, 1, factors, max_factors);
    b = bs = 0;
    while (factors[b] > 0) {
      b++;
      bs += factors[b];
    }
    if ((a < b) || ((a == b) && (as < bs))) {
      communication_factors_sub(number, max_factor, 0, factors, max_factors);
    }
  }
}

int get_num_cores_per_node(MPI_Comm comm) {
  int my_mpi_rank, num_cores, num_cores_min, num_cores_max;
  MPI_Comm comm_node;
  MPI_Info info;
  MPI_Comm_rank(comm, &my_mpi_rank);
  MPI_Info_create(&info);
  MPI_Comm_split_type(comm, MPI_COMM_TYPE_SHARED, my_mpi_rank, info,
                      &comm_node);
  MPI_Info_free(&info);
  MPI_Comm_size(comm_node, &num_cores);
  MPI_Comm_free(&comm_node);
  MPI_Allreduce(&num_cores, &num_cores_min, 1, MPI_INT, MPI_MIN, comm);
  MPI_Allreduce(&num_cores, &num_cores_max, 1, MPI_INT, MPI_MAX, comm);
  if (num_cores_min == num_cores_max) {
    return (num_cores);
  } else {
    return (-1);
  }
}

int break_tiles(int message_size, int *tmsize, int *my_cores_per_node_row,
                int *my_cores_per_node_column) {
  int i;
  (*tmsize) *= (*my_cores_per_node_row) * (*my_cores_per_node_row) *
               (*my_cores_per_node_column);
  if ((*my_cores_per_node_column) == 1) {
    i = 2;
    while ((*tmsize > message_size) && (i <= *my_cores_per_node_row)) {
      if ((*my_cores_per_node_row) % i == 0) {
        (*my_cores_per_node_row) /= i;
        (*tmsize) /= (i * i);
      } else {
        i++;
      }
    }
  }
  if (*my_cores_per_node_row < 1) {
    *my_cores_per_node_row = 1;
  }
  return (1);
  return (*tmsize <= message_size);
}

int EXT_MPI_Init() {
  read_bench();
  read_env();
  is_initialised = 1;
  return (0);
}

int EXT_MPI_Initialized(int *flag) {
  *flag = is_initialised;
  return (0);
}

int EXT_MPI_Finalize() { return (0); }

int EXT_MPI_Alltoall_init_general(void *sendbuf, int sendcount,
                                  MPI_Datatype sendtype, void *recvbuf,
                                  int recvcount, MPI_Datatype recvtype,
                                  MPI_Comm comm_row, int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column, int *handle) {
  int my_mpi_size_row, num_ports, type_size, tmsize, chunks_throttle;
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  MPI_Type_size(sendtype, &type_size);
  tmsize = type_size * sendcount;
  if (tmsize < 1) {
    tmsize = 1;
  }
  tmsize *=
      my_cores_per_node_row * my_cores_per_node_row * my_cores_per_node_column;
  num_ports = my_mpi_size_row / my_cores_per_node_row - 1;
  if ((tmsize < MERGE_SIZE) &&
      (num_ports >= my_cores_per_node_row * my_cores_per_node_column)) {
    while ((tmsize < MERGE_SIZE) &&
           (num_ports >= my_cores_per_node_row * my_cores_per_node_column)) {
      tmsize *= 2;
      num_ports = (num_ports - 1) / 4 + 1;
    }
    if (num_ports < my_cores_per_node_row * my_cores_per_node_column) {
      num_ports = my_cores_per_node_row * my_cores_per_node_column;
    }
    chunks_throttle =
        (num_ports / (my_cores_per_node_row * my_cores_per_node_column)) / 3 +
        1;
    *handle = EXT_MPI_Alltoall_init_native(
        sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm_row,
        my_cores_per_node_row, comm_column, my_cores_per_node_column, num_ports,
        my_cores_per_node_row * my_cores_per_node_column, chunks_throttle);
    return (0);
  }
  tmsize = type_size * sendcount;
  if (break_tiles(BREAK_SIZE, &tmsize, &my_cores_per_node_row,
                  &my_cores_per_node_column)) {
    num_ports = my_mpi_size_row / my_cores_per_node_row - 1;
    chunks_throttle =
        (num_ports / (my_cores_per_node_row * my_cores_per_node_column)) / 3 +
        1;
    *handle = EXT_MPI_Alltoall_init_native(
        sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm_row,
        my_cores_per_node_row, comm_column, my_cores_per_node_column, num_ports,
        my_cores_per_node_row * my_cores_per_node_column, chunks_throttle);
  } else {
    *handle = -1;
  }
  return (0);
}

int EXT_MPI_Alltoallv_init_general(void *sendbuf, int *sendcounts, int *sdispls,
                                   MPI_Datatype sendtype, void *recvbuf,
                                   int *recvcounts, int *rdispls,
                                   MPI_Datatype recvtype, MPI_Comm comm_row,
                                   int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column, int *handle) {
  int my_mpi_size_row, chunks_throttle, element_max_size, type_size, tmsize, i,
      j;
#ifdef DEBUG
  int world_rank, sum_sendcounts, sum_recvcounts, k, new_counts_displs;
  void *recvbuf_ref;
#endif
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  element_max_size = 0;
  for (i = 0; i < my_mpi_size_row; i++) {
    if (sendcounts[i] > element_max_size) {
      element_max_size = sendcounts[i];
    }
  }
  MPI_Type_size(sendtype, &type_size);
  tmsize = type_size * element_max_size;
  MPI_Allreduce(MPI_IN_PLACE, &tmsize, 1, MPI_INT, MPI_MAX, comm_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Allreduce(MPI_IN_PLACE, &tmsize, 1, MPI_INT, MPI_MAX, comm_column);
  }
  i = my_cores_per_node_row;
  j = my_cores_per_node_column;
  if (break_tiles(BREAK_SIZE, &tmsize, &my_cores_per_node_row,
                  &my_cores_per_node_column)) {
    chunks_throttle = ((my_mpi_size_row / my_cores_per_node_row - 1) /
                       (my_cores_per_node_row * my_cores_per_node_column)) /
                          3 +
                      1;
    chunks_throttle = 2;
    if ((fixed_tile_size_row > 0) && (i % fixed_tile_size_row == 0)) {
      my_cores_per_node_row = fixed_tile_size_row;
    }
    if ((fixed_tile_size_column > 0) && (j % fixed_tile_size_column == 0)) {
      my_cores_per_node_column = fixed_tile_size_column;
    }
    *handle = EXT_MPI_Alltoallv_init_native(
        sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts, rdispls,
        recvtype, comm_row, my_cores_per_node_row, comm_column,
        my_cores_per_node_column,
        my_cores_per_node_row * my_cores_per_node_column, chunks_throttle);
  } else {
    *handle = -1;
  }
#ifdef DEBUG
  new_counts_displs = (sdispls == NULL);
  if (new_counts_displs) {
    sdispls = (int *)malloc(my_mpi_size_row * sizeof(int));
    recvcounts = (int *)malloc(my_mpi_size_row * sizeof(int));
    rdispls = (int *)malloc(my_mpi_size_row * sizeof(int));
    MPI_Alltoall(sendcounts, 1, MPI_INT, recvcounts, 1, MPI_INT, comm_row);
    sdispls[0] = 0;
    rdispls[0] = 0;
    for (i = 0; i < my_mpi_size_row - 1; i++) {
      sdispls[i + 1] = sdispls[i] + sendcounts[i];
      rdispls[i + 1] = rdispls[i] + recvcounts[i];
    }
  }
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  sum_sendcounts = 0;
  sum_recvcounts = 0;
  for (i = 0; i < my_mpi_size_row; i++) {
    sum_sendcounts += sendcounts[i];
    sum_recvcounts += recvcounts[i];
  }
  recvbuf_ref = (void *)malloc(sum_recvcounts * type_size);
  k = 0;
  for (j = 0; j < my_mpi_size_row; j++) {
    for (i = 0; i < (int)((sendcounts[j] * type_size) / sizeof(long int));
         i++) {
      ((long int *)sendbuf)[(sdispls[j] * type_size) / sizeof(long int) + i] =
          world_rank * ((long int)sum_sendcounts * type_size) /
              sizeof(long int) +
          k;
      k++;
    }
  }
  MPI_Alltoallv(sendbuf, sendcounts, sdispls, sendtype, recvbuf_ref, recvcounts,
                rdispls, recvtype, comm_row);
  EXT_MPI_Exec_native(*handle);
  k = 0;
  for (j = 0; j < my_mpi_size_row; j++) {
    for (i = 0; i < (int)((recvcounts[j] * type_size) / sizeof(long int));
         i++) {
      if (((long int *)
               recvbuf)[(rdispls[j] * type_size) / sizeof(long int) + i] !=
          ((long int *)
               recvbuf_ref)[(rdispls[j] * type_size) / sizeof(long int) + i]) {
        k = 1;
      }
    }
  }
  if (k) {
    printf("logical error in EXT_MPI_Alltoallv %d\n", world_rank);
    exit(1);
  }
  free(recvbuf_ref);
  if (new_counts_displs) {
    free(rdispls);
    free(recvcounts);
    free(sdispls);
  }
#endif
  return (0);
}

int EXT_MPI_Ialltoall_init_general(void *sendbuf, int sendcount,
                                   MPI_Datatype sendtype, void *recvbuf,
                                   int recvcount, MPI_Datatype recvtype,
                                   MPI_Comm comm_row, int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column,
                                   int *handle_begin, int *handle_wait) {
  int type_size, tmsize;
  MPI_Type_size(sendtype, &type_size);
  tmsize = type_size * sendcount;
  if (break_tiles(BREAK_SIZE, &tmsize, &my_cores_per_node_row,
                  &my_cores_per_node_column)) {
    EXT_MPI_Ialltoall_init_native(
        sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm_row,
        my_cores_per_node_row, comm_column, my_cores_per_node_column,
        my_cores_per_node_row * my_cores_per_node_column, handle_begin,
        handle_wait);
  } else {
    *handle_begin = *handle_wait = -1;
  }
  return (0);
}

int EXT_MPI_Ialltoallv_init_general(
    void *sendbuf, int *sendcounts, int *sdispls, MPI_Datatype sendtype,
    void *recvbuf, int *recvcounts, int *rdispls, MPI_Datatype recvtype,
    MPI_Comm comm_row, int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *handle_begin, int *handle_wait) {
  int my_mpi_size_row, element_max_size, type_size, tmsize, i;
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  element_max_size = 0;
  for (i = 0; i < my_mpi_size_row; i++) {
    if (sendcounts[i] > element_max_size) {
      element_max_size = sendcounts[i];
    }
  }
  MPI_Type_size(sendtype, &type_size);
  tmsize = type_size * element_max_size;
  MPI_Allreduce(MPI_IN_PLACE, &tmsize, 1, MPI_INT, MPI_MAX, comm_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Allreduce(MPI_IN_PLACE, &tmsize, 1, MPI_INT, MPI_MAX, comm_column);
  }
  if (break_tiles(BREAK_SIZE, &tmsize, &my_cores_per_node_row,
                  &my_cores_per_node_column)) {
    EXT_MPI_Ialltoallv_init_native(
        sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts, rdispls,
        recvtype, comm_row, my_cores_per_node_row, comm_column,
        my_cores_per_node_column,
        my_cores_per_node_row * my_cores_per_node_column, handle_begin,
        handle_wait);
  } else {
    *handle_begin = *handle_wait = -1;
  }
  return (0);
}

int EXT_MPI_Alltoall_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                          void *recvbuf, int recvcount, MPI_Datatype recvtype,
                          MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Alltoall_init_general(
        sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm,
        num_core_per_node, MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Alltoallv_init(void *sendbuf, int *sendcounts, int *sdispls,
                           MPI_Datatype sendtype, void *recvbuf,
                           int *recvcounts, int *rdispls, MPI_Datatype recvtype,
                           MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Alltoallv_init_general(
        sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts, rdispls,
        recvtype, comm, num_core_per_node, MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Ialltoall_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                           void *recvbuf, int recvcount, MPI_Datatype recvtype,
                           MPI_Comm comm, int *handle_begin, int *handle_wait) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Ialltoall_init_general(
        sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm,
        num_core_per_node, MPI_COMM_NULL, 1, handle_begin, handle_wait));
  } else {
    *handle_begin = *handle_wait = -1;
    return (0);
  }
}

int EXT_MPI_Ialltoallv_init(void *sendbuf, int *sendcounts, int *sdispls,
                            MPI_Datatype sendtype, void *recvbuf,
                            int *recvcounts, int *rdispls,
                            MPI_Datatype recvtype, MPI_Comm comm,
                            int *handle_begin, int *handle_wait) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Ialltoallv_init_general(
        sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts, rdispls,
        recvtype, comm, num_core_per_node, MPI_COMM_NULL, 1, handle_begin,
        handle_wait));
  } else {
    *handle_begin = *handle_wait = -1;
    return (0);
  }
}

int EXT_MPI_Scatter_init_general(void *sendbuf, int sendcount,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 int recvcount, MPI_Datatype recvtype, int root,
                                 MPI_Comm comm_row, int my_cores_per_node_row,
                                 MPI_Comm comm_column,
                                 int my_cores_per_node_column, int *handle) {
  int *sendcounts, *sdispls, *recvcounts, *rdispls;
  int mpi_comm_rank, mpi_comm_size;
  int returnvalue, i;
  MPI_Comm_rank(comm_row, &mpi_comm_rank);
  MPI_Comm_size(comm_row, &mpi_comm_size);
  sendcounts = (int *)malloc(mpi_comm_size * sizeof(int));
  sdispls = (int *)malloc(mpi_comm_size * sizeof(int));
  recvcounts = (int *)malloc(mpi_comm_size * sizeof(int));
  rdispls = (int *)malloc(mpi_comm_size * sizeof(int));
  if (mpi_comm_rank == root) {
    for (i = 0; i < mpi_comm_size; i++) {
      sendcounts[i] = sendcount;
    }
  } else {
    for (i = 0; i < mpi_comm_size; i++) {
      sendcounts[i] = 0;
    }
  }
  for (i = 0; i < mpi_comm_size; i++) {
    recvcounts[i] = 0;
  }
  recvcounts[root] = recvcount;
  sdispls[0] = rdispls[0] = 0;
  for (i = 0; i < mpi_comm_size - 1; i++) {
    sdispls[i + 1] = sdispls[i] + sendcounts[i];
    rdispls[i + 1] = rdispls[i] + recvcounts[i];
  }
  returnvalue = EXT_MPI_Alltoallv_init_general(
      sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts, rdispls,
      recvtype, comm_row, my_cores_per_node_row, comm_column,
      my_cores_per_node_column, handle);
  free(rdispls);
  free(recvcounts);
  free(sdispls);
  free(sendcounts);
  return (returnvalue);
}

int EXT_MPI_Gather_init_general(void *sendbuf, int sendcount,
                                MPI_Datatype sendtype, void *recvbuf,
                                int recvcount, MPI_Datatype recvtype, int root,
                                MPI_Comm comm_row, int my_cores_per_node_row,
                                MPI_Comm comm_column,
                                int my_cores_per_node_column, int *handle) {
  int *sendcounts, *sdispls, *recvcounts, *rdispls;
  int mpi_comm_rank, mpi_comm_size;
  int returnvalue, i;
  MPI_Comm_rank(comm_row, &mpi_comm_rank);
  MPI_Comm_size(comm_row, &mpi_comm_size);
  sendcounts = (int *)malloc(mpi_comm_size * sizeof(int));
  sdispls = (int *)malloc(mpi_comm_size * sizeof(int));
  recvcounts = (int *)malloc(mpi_comm_size * sizeof(int));
  rdispls = (int *)malloc(mpi_comm_size * sizeof(int));
  if (mpi_comm_rank == root) {
    for (i = 0; i < mpi_comm_size; i++) {
      recvcounts[i] = recvcount;
    }
  } else {
    for (i = 0; i < mpi_comm_size; i++) {
      recvcounts[i] = 0;
    }
  }
  for (i = 0; i < mpi_comm_size; i++) {
    sendcounts[i] = 0;
  }
  sendcounts[root] = sendcount;
  sdispls[0] = rdispls[0] = 0;
  for (i = 0; i < mpi_comm_size - 1; i++) {
    sdispls[i + 1] = sdispls[i] + sendcounts[i];
    rdispls[i + 1] = rdispls[i] + recvcounts[i];
  }
  returnvalue = EXT_MPI_Alltoallv_init_general(
      sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts, rdispls,
      recvtype, comm_row, my_cores_per_node_row, comm_column,
      my_cores_per_node_column, handle);
  free(rdispls);
  free(recvcounts);
  free(sdispls);
  free(sendcounts);
  return (returnvalue);
}

int EXT_MPI_Scatter_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                         void *recvbuf, int recvcount, MPI_Datatype recvtype,
                         int root, MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Scatter_init_general(
        sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, root, comm,
        num_core_per_node, MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Gather_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                        void *recvbuf, int recvcount, MPI_Datatype recvtype,
                        int root, MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Gather_init_general(
        sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, root, comm,
        num_core_per_node, MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

static int EXT_MPI_Allgatherv_init_general_ext(
    void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    int *recvcounts, int *displs, MPI_Datatype recvtype, MPI_Comm comm_row,
    int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *handle, int allreduce, int recvcount) {
  int comm_size_row, *num_ports, *num_parallel, type_size, scount, i;
#ifdef DEBUG
  void *recvbuf_ref;
  int world_rank, comm_rank_row, max_sendcount, max_displs, j, k;
#endif
#ifdef VERBOSE
  int world_rankv;
#endif
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Type_size(sendtype, &type_size);
  num_ports = (int *)malloc((comm_size_row + 1) * sizeof(int));
  num_parallel = (int *)malloc((comm_size_row + 1) * sizeof(int));
  MPI_Allreduce(&sendcount, &scount, 1, MPI_INT, MPI_MAX, comm_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Allreduce(MPI_IN_PLACE, &scount, 1, MPI_INT, MPI_SUM, comm_column);
  }
  if (scount * type_size <= 25000000) {
    if (fixed_factors == NULL) {
      cost_recursive(comm_size_row / my_cores_per_node_row, scount * type_size,
                     1, my_cores_per_node_row * my_cores_per_node_column,
                     num_ports);
    } else {
      i = -1;
      do {
        i++;
        num_ports[i] = fixed_factors[i];
      } while (fixed_factors[i] > 0);
    }
    i = 0;
    while (num_ports[i] != 0) {
      num_ports[i]--;
      i++;
    }
#ifdef VERBOSE
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rankv);
    if (world_rankv == 0) {
      printf("# allgatherv parameters %d %d %d %d ports ",
             comm_size_row / my_cores_per_node_row, sendcount * type_size, 1,
             my_cores_per_node_row * my_cores_per_node_column);
      i = 0;
      while (num_ports[i] > 0) {
        printf("%d ", num_ports[i]);
        i++;
      }
      printf("\n");
    }
#endif
    if (my_cores_per_node_row * my_cores_per_node_column > 1) {
      for (i = 0; i < comm_size_row / my_cores_per_node_row + 1; i++) {
        //      num_parallel[i] =
        //      (my_cores_per_node_row*my_cores_per_node_column)/(num_ports[i]+1);
        num_parallel[i] = 1;
      }
      if (recvcount < 0) {
        *handle = EXT_MPI_Allgatherv_init_native(
            sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
            comm_row, my_cores_per_node_row, comm_column,
            my_cores_per_node_column, num_ports, num_parallel,
            my_cores_per_node_row * my_cores_per_node_column, allreduce,
            enforce_bruck, -1);
      } else {
        *handle = EXT_MPI_Allgatherv_init_native(
            sendbuf, sendcount, sendtype, recvbuf, NULL, displs, recvtype,
            comm_row, my_cores_per_node_row, comm_column,
            my_cores_per_node_column, num_ports, num_parallel,
            my_cores_per_node_row * my_cores_per_node_column, allreduce,
            enforce_bruck, recvcount);
      }
    } else {
      for (i = 0; i < comm_size_row / my_cores_per_node_row + 1; i++) {
        num_ports[i] = (comm_size_row / my_cores_per_node_row) / 2 - 1;
        num_ports[i] = 1;
        num_parallel[i] = 1;
      }
      if (recvcount < 0) {
        *handle = EXT_MPI_Allgatherv_init_native(
            sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
            comm_row, 1, MPI_COMM_NULL, 1, num_ports, num_parallel, 1,
            allreduce, enforce_bruck, -1);
      } else {
        *handle = EXT_MPI_Allgatherv_init_native(
            sendbuf, sendcount, sendtype, recvbuf, NULL, displs, recvtype,
            comm_row, 1, MPI_COMM_NULL, 1, num_ports, num_parallel, 1,
            allreduce, enforce_bruck, recvcount);
      }
    }
  } else {
    for (i = 0; i < comm_size_row + 1; i++) {
      num_ports[i] = comm_size_row - 1;
      num_ports[i] = 3;
    }
    for (i = 0; i < comm_size_row / my_cores_per_node_row + 1; i++) {
      //      num_parallel[i] =
      //      (my_cores_per_node_row*my_cores_per_node_column)/(num_ports[i]+1);
      num_parallel[i] = 1;
    }
    if (recvcount < 0) {
      *handle = EXT_MPI_Allgatherv_init_native(
          sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
          comm_row, 1, MPI_COMM_NULL, 1, num_ports, num_parallel, 1, allreduce,
          enforce_bruck, -1);
    } else {
      *handle = EXT_MPI_Allgatherv_init_native(
          sendbuf, sendcount, sendtype, recvbuf, NULL, displs, recvtype,
          comm_row, 1, MPI_COMM_NULL, 1, num_ports, num_parallel, 1, allreduce,
          enforce_bruck, recvcount);
    }
  }
  free(num_parallel);
  free(num_ports);
#ifdef DEBUG
  if (!allreduce) {
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_rank(comm_row, &comm_rank_row);
    if (recvcount < 0) {
      max_sendcount = recvcounts[0];
      max_displs = 0;
      for (i = 0; i < comm_size_row; i++) {
        if (recvcounts[i] > max_sendcount)
          max_sendcount = recvcounts[i];
        if (displs[i] > max_displs)
          max_displs = displs[i];
      }
      j = max_displs + max_sendcount;
    } else {
      max_sendcount = recvcount;
      j = recvcount * comm_size_row;
    }
    recvbuf_ref = (void *)malloc(j * type_size);
    for (i = 0; i < (int)((sendcount * type_size) / sizeof(long int)); i++) {
      ((long int *)sendbuf)[i] = world_rank * max_sendcount + i;
    }
    if (recvcount < 0) {
      MPI_Allgatherv(sendbuf, sendcount, sendtype, recvbuf_ref, recvcounts,
                     displs, recvtype, comm_row);
    } else {
      MPI_Allgather(sendbuf, sendcount, sendtype, recvbuf_ref, recvcount,
                    recvtype, comm_row);
    }
    EXT_MPI_Exec_native(*handle);
    k = 0;
    if (recvcount < 0) {
      for (j = 0; j < comm_size_row; j++) {
        for (i = 0; i < (int)((recvcounts[j] * type_size) / sizeof(long int));
             i++) {
          if (((long int *)
                   recvbuf)[(displs[j] * type_size) / sizeof(long int) + i] !=
              ((long int *)
                   recvbuf_ref)[(displs[j] * type_size) / sizeof(long int) +
                                i]) {
            k = 1;
          }
        }
      }
    } else {
      for (j = 0; j < comm_size_row; j++) {
        for (i = 0; i < (int)((recvcount * type_size) / sizeof(long int));
             i++) {
          if (((long int *)
                   recvbuf)[(j * recvcount * type_size) / sizeof(long int) +
                            i] !=
              ((long int *)
                   recvbuf_ref)[(j * recvcount * type_size) / sizeof(long int) +
                                i]) {
            k = 1;
          }
        }
      }
    }
    if (k) {
      printf("logical error in EXT_MPI_Allgatherv %d\n", world_rank);
      exit(1);
    }
    free(recvbuf_ref);
  }
#endif
  return (0);
}

static int EXT_MPI_Reduce_scatter_init_general_ext(
    void *sendbuf, void *recvbuf, int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *handle,
    int allreduce, int recvcount) {
  int comm_size_row, *num_ports, *num_parallel, type_size, rcount, i, j;
#ifdef DEBUG
  void *recvbuf_ref;
  int world_rank, comm_rank_row, k;
  if ((op != MPI_SUM) || (datatype != MPI_LONG)) {
    if (recvcount < 0) {
      EXT_MPI_Reduce_scatter_init_general(
          sendbuf, recvbuf, recvcounts, MPI_LONG, MPI_SUM, comm_row,
          my_cores_per_node_row, comm_column, my_cores_per_node_column, handle);
    } else {
      EXT_MPI_Reduce_scatter_block_init_general(
          sendbuf, recvbuf, recvcount, MPI_LONG, MPI_SUM, comm_row,
          my_cores_per_node_row, comm_column, my_cores_per_node_column, handle);
    }
    EXT_MPI_Done_native(*handle);
  }
#endif
#ifdef VERBOSE
  int world_rankv;
#endif
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Type_size(datatype, &type_size);
  num_ports = (int *)malloc((comm_size_row + 1) * sizeof(int));
  num_parallel = (int *)malloc((comm_size_row + 1) * sizeof(int));
  if (recvcount < 0) {
    rcount = 0;
    for (i = 0; i < comm_size_row; i++) {
      if (recvcounts[i] > rcount) {
        rcount = recvcounts[i];
      }
    }
    if (comm_column != MPI_COMM_NULL) {
      MPI_Allreduce(MPI_IN_PLACE, &rcount, 1, MPI_INT, MPI_SUM, comm_column);
    }
  } else {
    rcount = recvcount * my_cores_per_node_column;
  }
  if (rcount * type_size <= 25000000) {
    if (fixed_factors == NULL) {
      cost_recursive(comm_size_row / my_cores_per_node_row, rcount * type_size,
                     1, my_cores_per_node_row * my_cores_per_node_column,
                     num_ports);
    } else {
      i = -1;
      do {
        i++;
        num_ports[i] = fixed_factors[i];
      } while (fixed_factors[i] > 0);
    }
    i = 0;
    while (num_ports[i] != 0) {
      num_ports[i]--;
      i++;
    }
#ifdef VERBOSE
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rankv);
    if (world_rankv == 0) {
      i = recvcount;
      if (i < 0) {
        i = recvcounts[0];
      }
      printf("# reduce_scatter parameters %d %d %d %d ports ",
             comm_size_row / my_cores_per_node_row, i * type_size, 1,
             my_cores_per_node_row * my_cores_per_node_column);
      i = 0;
      while (num_ports[i] > 0) {
        printf("%d ", num_ports[i]);
        i++;
      }
      printf("\n");
    }
#endif
    if (my_cores_per_node_row * my_cores_per_node_column > 1) {
      for (i = 0; i < comm_size_row / my_cores_per_node_row + 1; i++) {
        //      num_parallel[i] =
        //      (my_cores_per_node_row*my_cores_per_node_column)/(num_ports[i]+1);
        num_parallel[i] = 1;
      }
      *handle = EXT_MPI_Reduce_scatter_init_native(
          sendbuf, recvbuf, recvcounts, datatype, op, comm_row,
          my_cores_per_node_row, comm_column, my_cores_per_node_column,
          num_ports, num_parallel,
          my_cores_per_node_row * my_cores_per_node_column, copyin_method,
          allreduce, enforce_bruck, recvcount);
    } else {
      for (i = 0; i < comm_size_row / my_cores_per_node_row + 1; i++) {
        num_ports[i] = (comm_size_row / my_cores_per_node_row) / 2 - 1;
        num_ports[i] = 1;
        num_parallel[i] = 1;
      }
      *handle = EXT_MPI_Reduce_scatter_init_native(
          sendbuf, recvbuf, recvcounts, datatype, op, comm_row, 1,
          MPI_COMM_NULL, 1, num_ports, num_parallel, 1, copyin_method,
          allreduce, enforce_bruck, recvcount);
    }
  } else {
    for (i = 0; i < comm_size_row + 1; i++) {
      num_ports[i] = comm_size_row - 1;
      num_ports[i] = 3;
    }
    for (i = 0; i < comm_size_row / my_cores_per_node_row + 1; i++) {
      //      num_parallel[i] =
      //      (my_cores_per_node_row*my_cores_per_node_column)/(num_ports[i]+1);
      num_parallel[i] = 1;
    }
    *handle = EXT_MPI_Reduce_scatter_init_native(
        sendbuf, recvbuf, recvcounts, datatype, op, comm_row, 1, MPI_COMM_NULL,
        1, num_ports, num_parallel, 1, copyin_method, allreduce, enforce_bruck,
        recvcount);
  }
  free(num_parallel);
  free(num_ports);
#ifdef DEBUG
  if (!allreduce) {
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_rank(comm_row, &comm_rank_row);
    if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
      if (recvcount < 0) {
        j = 0;
        for (i = 0; i < comm_size_row; i++) {
          j += recvcounts[i];
        }
        recvbuf_ref =
            (long int *)malloc(recvcounts[comm_rank_row] * sizeof(long int));
      } else {
        j = recvcount * comm_size_row;
        recvbuf_ref = (long int *)malloc(recvcount * sizeof(long int));
      }
      for (i = 0; i < j; i++) {
        ((long int *)sendbuf)[i] = world_rank * j + i;
      }
      if (recvcount < 0) {
        MPI_Reduce_scatter(sendbuf, recvbuf_ref, recvcounts, MPI_LONG, MPI_SUM,
                           comm_row);
      } else {
        MPI_Reduce_scatter_block(sendbuf, recvbuf_ref, recvcount, MPI_LONG,
                                 MPI_SUM, comm_row);
      }
      EXT_MPI_Exec_native(*handle);
      if (recvcount < 0) {
        k = recvcounts[comm_rank_row];
      } else {
        k = recvcount;
      }
      j = 0;
      for (i = 0; i < k; i++) {
        if (((long int *)recvbuf)[i] != ((long int *)recvbuf_ref)[i]) {
          j = 1;
        }
      }
      if (j) {
        printf("logical error in EXT_MPI_Reduce_scatter %d\n", world_rank);
        exit(1);
      }
      free(recvbuf_ref);
    }
  }
#endif
  return (0);
}

int EXT_MPI_Allgatherv_init_general(void *sendbuf, int sendcount,
                                    MPI_Datatype sendtype, void *recvbuf,
                                    int *recvcounts, int *displs,
                                    MPI_Datatype recvtype, MPI_Comm comm_row,
                                    int my_cores_per_node_row,
                                    MPI_Comm comm_column,
                                    int my_cores_per_node_column, int *handle) {
  return (EXT_MPI_Allgatherv_init_general_ext(
      sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
      comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column,
      handle, 0, -1));
}

int EXT_MPI_Allgather_init_general(void *sendbuf, int sendcount,
                                   MPI_Datatype sendtype, void *recvbuf,
                                   int recvcount, MPI_Datatype recvtype,
                                   MPI_Comm comm_row, int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column, int *handle) {
  return (EXT_MPI_Allgatherv_init_general_ext(
      sendbuf, sendcount, sendtype, recvbuf, NULL, NULL, recvtype, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, handle, 0,
      recvcount));
}

int EXT_MPI_Reduce_scatter_init_general(
    void *sendbuf, void *recvbuf, int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *handle) {
  return (EXT_MPI_Reduce_scatter_init_general_ext(
      sendbuf, recvbuf, recvcounts, datatype, op, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, handle, 0,
      -1));
}

int EXT_MPI_Reduce_scatter_block_init_general(
    void *sendbuf, void *recvbuf, int recvcount, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *handle) {
  return (EXT_MPI_Reduce_scatter_init_general_ext(
      sendbuf, recvbuf, NULL, datatype, op, comm_row, my_cores_per_node_row,
      comm_column, my_cores_per_node_column, handle, 0, recvcount));
}

int EXT_MPI_Allreduce_init_general(void *sendbuf, void *recvbuf, int count,
                                   MPI_Datatype datatype, MPI_Op op,
                                   MPI_Comm comm_row, int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column, int *handle) {
  int comm_size_row, comm_rank_row, i, j;
  int *recvcounts, *displs, message_size;
  int *num_ports, *num_ports_limit;
  int handle1, handle2;
#ifdef VERBOSE
  int world_rank;
#endif
#ifdef DEBUG
  int world_rankd;
  void *recvbuf_ref;
  if ((op != MPI_SUM) || (datatype != MPI_LONG)) {
    EXT_MPI_Allreduce_init_general(sendbuf, recvbuf, count, MPI_LONG, MPI_SUM,
                                   comm_row, my_cores_per_node_row, comm_column,
                                   my_cores_per_node_column, handle);
    EXT_MPI_Done_native(*handle);
  }
#endif
  MPI_Comm_size(comm_row, &comm_size_row);
  MPI_Comm_rank(comm_row, &comm_rank_row);
  MPI_Type_size(datatype, &message_size);
  message_size *= count;
  if (comm_column != MPI_COMM_NULL) {
    MPI_Allreduce(MPI_IN_PLACE, &message_size, 1, MPI_INT, MPI_SUM,
                  comm_column);
  }
  if (message_size <= threshold_rabenseifner) {
    num_ports = (int *)malloc(comm_size_row * sizeof(int));
    num_ports_limit = (int *)malloc(comm_size_row * sizeof(int));
    for (i = 0; i < comm_size_row; i++) {
      num_ports[i] = comm_size_row / my_cores_per_node_row - 1;
      num_ports_limit[i] = comm_size_row / my_cores_per_node_row - 1;
    }
    for (i = 0; i < comm_size_row; i++) {
      num_ports[i] = num_ports_limit[i] = 0;
    }
    if ((fixed_factors == NULL) || (fixed_factors_limit == NULL)) {
      communication_factors(comm_size_row / my_cores_per_node_row,
                            my_cores_per_node_row * my_cores_per_node_column +
                                1,
                            num_ports, num_ports_limit);
    } else {
      if (fixed_factors != NULL) {
        i = -1;
        do {
          i++;
          num_ports[i] = fixed_factors[i];
        } while (fixed_factors[i] > 0);
      }
      if (fixed_factors_limit != NULL) {
        i = -1;
        do {
          i++;
          num_ports_limit[i] = fixed_factors_limit[i];
        } while (fixed_factors_limit[i] > 0);
      }
    }
    i = 0;
    while (num_ports[i] != 0) {
      num_ports[i]--;
      i++;
    }
    i = 0;
    while (num_ports_limit[i] != 0) {
      num_ports_limit[i]--;
      i++;
    }
#ifdef VERBOSE
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    if (world_rank == 0) {
      printf("# allreduce parameters %d %d %d %d ports ",
             comm_size_row / my_cores_per_node_row, count * message_size, 1,
             my_cores_per_node_row * my_cores_per_node_column);
      i = 0;
      while (num_ports[i] > 0) {
        printf("%d ", num_ports[i]);
        i++;
      }
      printf("ports_limit ");
      i = 0;
      while (num_ports_limit[i] > 0) {
        printf("%d ", num_ports_limit[i]);
        i++;
      }
      printf("\n");
    }
#endif
    *handle = EXT_MPI_Allreduce_init_native(
        sendbuf, recvbuf, count, datatype, op, comm_row, my_cores_per_node_row,
        comm_column, my_cores_per_node_column, num_ports, num_ports_limit,
        my_cores_per_node_row * my_cores_per_node_column, copyin_method);
    free(num_ports_limit);
    free(num_ports);
  } else {
    recvcounts = (int *)malloc(comm_size_row * sizeof(int));
    displs = (int *)malloc(comm_size_row * sizeof(int));
    for (i = 0; i < comm_size_row; i++) {
      recvcounts[i] = count / comm_size_row;
      if (i < count % comm_size_row) {
        recvcounts[i]++;
      }
    }
    EXT_MPI_Reduce_scatter_init_general_ext(
        sendbuf, recvbuf, recvcounts, datatype, op, comm_row,
        my_cores_per_node_row, comm_column, my_cores_per_node_column, &handle1,
        1, -1);
    displs[0] = 0;
    for (i = 0; i < comm_size_row - 1; i++) {
      displs[i + 1] = displs[i] + recvcounts[i];
    }
    EXT_MPI_Allgatherv_init_general_ext(
        recvbuf, recvcounts[comm_rank_row], datatype, recvbuf, recvcounts,
        displs, datatype, comm_row, my_cores_per_node_row, comm_column,
        my_cores_per_node_column, &handle2, 1, -1);
    free(displs);
    free(recvcounts);
    *handle = EXT_MPI_Merge_collectives_native(handle1, handle2);
  }
#ifdef DEBUG
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rankd);
  if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
    recvbuf_ref = (long int *)malloc(count * sizeof(long int));
    for (i = 0; i < count; i++) {
      ((long int *)sendbuf)[i] = world_rankd * count + i;
    }
    MPI_Allreduce(sendbuf, recvbuf_ref, count, MPI_LONG, MPI_SUM, comm_row);
    EXT_MPI_Exec_native(*handle);
    j = 0;
    for (i = 0; i < count; i++) {
      if (((long int *)recvbuf)[i] != ((long int *)recvbuf_ref)[i]) {
        j = 1;
      }
    }
    if (j) {
      printf("logical error in EXT_MPI_Allreduce %d\n", world_rankd);
      exit(1);
    }
    free(recvbuf_ref);
  }
#endif
  return (0);
}

int EXT_MPI_Bcast_init_general(void *sendbuf, int count, MPI_Datatype datatype,
                               int root, MPI_Comm comm_row,
                               int my_cores_per_node_row, MPI_Comm comm_column,
                               int my_cores_per_node_column, int *handle) {
  int *recvcounts, *displs;
  int comm_row_size, comm_row_rank, i;
  int iret;
  MPI_Comm_size(comm_row, &comm_row_size);
  MPI_Comm_rank(comm_row, &comm_row_rank);
  recvcounts = (int *)malloc(comm_row_size * sizeof(int));
  for (i = 0; i < comm_row_size; i++) {
    recvcounts[i] = 0;
  }
  recvcounts[root] = count;
  displs = (int *)malloc(comm_row_size * sizeof(int));
  displs[0] = 0;
  for (i = 0; i < comm_row_size - 1; i++) {
    displs[i + 1] = displs[i] + recvcounts[i];
  }
  count = recvcounts[comm_row_rank];
  iret = EXT_MPI_Allgatherv_init_general(
      sendbuf, count, datatype, sendbuf, recvcounts, displs, datatype, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, handle);
  free(displs);
  free(recvcounts);
  return (iret);
}

int EXT_MPI_Reduce_init_general(void *sendbuf, void *recvbuf, int count,
                                MPI_Datatype datatype, MPI_Op op, int root,
                                MPI_Comm comm_row, int my_cores_per_node_row,
                                MPI_Comm comm_column,
                                int my_cores_per_node_column, int *handle) {
  int *recvcounts;
  int comm_row_size, i;
  int iret;
  MPI_Comm_size(comm_row, &comm_row_size);
  recvcounts = (int *)malloc(comm_row_size * sizeof(int));
  for (i = 0; i < comm_row_size; i++) {
    recvcounts[i] = 0;
  }
  recvcounts[root] = count;
  iret = EXT_MPI_Reduce_scatter_init_general(
      sendbuf, recvbuf, recvcounts, datatype, op, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, handle);
  free(recvcounts);
  return (iret);
}

int EXT_MPI_Allgatherv_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                            void *recvbuf, int *recvcounts, int *displs,
                            MPI_Datatype recvtype, MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Allgatherv_init_general(
        sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
        comm, num_core_per_node, MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Allgather_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                           void *recvbuf, int recvcount, MPI_Datatype recvtype,
                           MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Allgather_init_general(
        sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm,
        num_core_per_node, MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Reduce_scatter_init(void *sendbuf, void *recvbuf, int *recvcounts,
                                MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
                                int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Reduce_scatter_init_general(
        sendbuf, recvbuf, recvcounts, datatype, op, comm, num_core_per_node,
        MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Reduce_scatter_block_init(void *sendbuf, void *recvbuf,
                                      int recvcount, MPI_Datatype datatype,
                                      MPI_Op op, MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Reduce_scatter_block_init_general(
        sendbuf, recvbuf, recvcount, datatype, op, comm, num_core_per_node,
        MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Allreduce_init(void *sendbuf, void *recvbuf, int count,
                           MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
                           int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Allreduce_init_general(sendbuf, recvbuf, count, datatype,
                                           op, comm, num_core_per_node,
                                           MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Bcast_init(void *sendbuf, int count, MPI_Datatype datatype,
                       int root, MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Bcast_init_general(sendbuf, count, datatype, root, comm,
                                       num_core_per_node, MPI_COMM_NULL, 1,
                                       handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Reduce_init(void *sendbuf, void *recvbuf, int count,
                        MPI_Datatype datatype, MPI_Op op, int root,
                        MPI_Comm comm, int *handle) {
  int num_core_per_node = get_num_cores_per_node(comm);
  if (num_core_per_node > 0) {
    return (EXT_MPI_Reduce_init_general(sendbuf, recvbuf, count, datatype, op,
                                        root, comm, num_core_per_node,
                                        MPI_COMM_NULL, 1, handle));
  } else {
    *handle = -1;
    return (0);
  }
}

int EXT_MPI_Exec(int handle) {
  if (handle >= 0) {
    return (EXT_MPI_Exec_native(handle));
  } else {
    return (0);
  }
}

int EXT_MPI_Done(int handle) {
  if (handle >= 0) {
    return (EXT_MPI_Done_native(handle));
  } else {
    return (0);
  }
}

int EXT_MPI_Init_general(MPI_Comm comm_row, int my_cores_per_node_row,
                         MPI_Comm comm_column, int my_cores_per_node_column,
                         int *handle) {
  *handle = EXT_MPI_Init_general_native(comm_row, my_cores_per_node_row,
                                        comm_column, my_cores_per_node_column);
  return (0);
}

int EXT_MPI_Done_general(int handle) {
  return (EXT_MPI_Done_general_native(handle));
}

int EXT_MPI_Allgatherv(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                       void *recvbuf, int *recvcounts, int *displs,
                       MPI_Datatype recvtype, int handle) {
  int my_cores_per_node_row, my_cores_per_node_column, my_mpi_size_row,
      my_mpi_rank_row;
  int num_ports[10000], num_parallel[10000], i, data_size, j;
  EXT_MPI_Get_size_rank_native(handle, &my_cores_per_node_row,
                               &my_cores_per_node_column, &my_mpi_size_row,
                               &my_mpi_rank_row);
  if (my_mpi_size_row / my_cores_per_node_row > 10000) {
    printf("maximum 10000 nodes\n");
    exit(1);
  }
  MPI_Type_size(recvtype, &data_size);
  j = 0;
  for (i = 0; i < my_mpi_size_row; i++) {
    if (recvcounts[i] > j) {
      j = recvcounts[i];
    }
  }
  data_size *= j;
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    num_ports[i] = my_cores_per_node_row * my_cores_per_node_column;
    num_parallel[i] = 1;
  }
  if (fixed_factors == NULL) {
    cost_explicit(my_mpi_size_row / my_cores_per_node_row, data_size,
                  my_cores_per_node_row * my_cores_per_node_column, num_ports);
  } else {
    i = -1;
    do {
      i++;
      num_ports[i] = fixed_factors[i];
    } while (fixed_factors[i] > 0);
  }
  return (EXT_MPI_Allgatherv_native(sendbuf, sendcount, sendtype, recvbuf,
                                    recvcounts, displs, recvtype, num_ports,
                                    num_parallel, handle));
}

int EXT_MPI_Allgather(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                      void *recvbuf, int recvcount, MPI_Datatype recvtype,
                      int handle) {
  int my_cores_per_node_row, my_cores_per_node_column, my_mpi_size_row,
      my_mpi_rank_row;
  int num_ports[10000], num_parallel[10000], i, data_size;
  EXT_MPI_Get_size_rank_native(handle, &my_cores_per_node_row,
                               &my_cores_per_node_column, &my_mpi_size_row,
                               &my_mpi_rank_row);
  if (my_mpi_size_row / my_cores_per_node_row > 10000) {
    printf("maximum 10000 nodes\n");
    exit(1);
  }
  MPI_Type_size(recvtype, &data_size);
  data_size *= sendcount;
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    num_ports[i] = my_cores_per_node_row * my_cores_per_node_column;
    num_parallel[i] = 1;
  }
  if (fixed_factors == NULL) {
    cost_explicit(my_mpi_size_row / my_cores_per_node_row, data_size,
                  my_cores_per_node_row * my_cores_per_node_column, num_ports);
  } else {
    i = -1;
    do {
      i++;
      num_ports[i] = fixed_factors[i];
    } while (fixed_factors[i] > 0);
  }
  return (EXT_MPI_Allgather_native(sendbuf, sendcount, sendtype, recvbuf,
                                   recvcount, recvtype, num_ports, num_parallel,
                                   handle));
}

int EXT_MPI_Reduce_scatter(void *sendbuf, void *recvbuf, int *recvcounts,
                           MPI_Datatype datatype, MPI_Op op, int handle) {
  int my_cores_per_node_row, my_cores_per_node_column, my_mpi_size_row,
      my_mpi_rank_row;
  int num_ports[10000], num_parallel[10000], i, data_size, j;
  EXT_MPI_Get_size_rank_native(handle, &my_cores_per_node_row,
                               &my_cores_per_node_column, &my_mpi_size_row,
                               &my_mpi_rank_row);
  if (my_mpi_size_row / my_cores_per_node_row > 10000) {
    printf("maximum 10000 nodes\n");
    exit(1);
  }
  MPI_Type_size(datatype, &data_size);
  j = 0;
  for (i = 0; i < my_mpi_size_row; i++) {
    if (recvcounts[i] > j) {
      j = recvcounts[i];
    }
  }
  data_size *= j;
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    num_ports[i] = my_cores_per_node_row * my_cores_per_node_column;
    num_parallel[i] = 1;
  }
  if (fixed_factors == NULL) {
    cost_explicit(my_mpi_size_row / my_cores_per_node_row, data_size,
                  my_cores_per_node_row * my_cores_per_node_column, num_ports);
  } else {
    i = -1;
    do {
      i++;
      num_ports[i] = fixed_factors[i];
    } while (fixed_factors[i] > 0);
  }
  return (EXT_MPI_Reduce_scatter_native(sendbuf, recvbuf, recvcounts, datatype,
                                        op, num_ports, num_parallel,
                                        copyin_method, handle));
}

int EXT_MPI_Reduce_scatter_block(void *sendbuf, void *recvbuf, int recvcount,
                                 MPI_Datatype datatype, MPI_Op op, int handle) {
  int my_cores_per_node_row, my_cores_per_node_column, my_mpi_size_row,
      my_mpi_rank_row;
  int num_ports[10000], num_parallel[10000], i, data_size;
  EXT_MPI_Get_size_rank_native(handle, &my_cores_per_node_row,
                               &my_cores_per_node_column, &my_mpi_size_row,
                               &my_mpi_rank_row);
  if (my_mpi_size_row / my_cores_per_node_row > 10000) {
    printf("maximum 10000 nodes\n");
    exit(1);
  }
  MPI_Type_size(datatype, &data_size);
  data_size *= recvcount;
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    num_ports[i] = my_cores_per_node_row * my_cores_per_node_column;
    num_parallel[i] = 1;
  }
  if (fixed_factors == NULL) {
    cost_explicit(my_mpi_size_row / my_cores_per_node_row, data_size,
                  my_cores_per_node_row * my_cores_per_node_column, num_ports);
  } else {
    i = -1;
    do {
      i++;
      num_ports[i] = fixed_factors[i];
    } while (fixed_factors[i] > 0);
  }
  return (EXT_MPI_Reduce_scatter_block_native(
      sendbuf, recvbuf, recvcount, datatype, op, num_ports, num_parallel,
      copyin_method, handle));
}

int EXT_MPI_Allreduce(void *sendbuf, void *recvbuf, int count,
                      MPI_Datatype datatype, MPI_Op op, int handle) {
  int my_cores_per_node_row, my_cores_per_node_column, my_mpi_size_row,
      my_mpi_rank_row;
  int num_ports[10000], num_ports_limit[10000], num_parallel[10000], i,
      data_size, j;
  int *recvcounts, *displs, retval;
  EXT_MPI_Get_size_rank_native(handle, &my_cores_per_node_row,
                               &my_cores_per_node_column, &my_mpi_size_row,
                               &my_mpi_rank_row);
  if (my_mpi_size_row / my_cores_per_node_row > 10000) {
    printf("maximum 10000 nodes\n");
    exit(1);
  }
  MPI_Type_size(datatype, &data_size);
  data_size *= count;
  if (data_size <= threshold_rabenseifner) {
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      num_ports[i] = num_ports_limit[i] = 0;
    }
    if (fixed_factors == NULL) {
      communication_factors(my_mpi_size_row / my_cores_per_node_row,
                            my_cores_per_node_row * my_cores_per_node_column +
                                1,
                            num_ports, num_ports_limit);
    } else {
      i = -1;
      do {
        i++;
        num_ports[i] = fixed_factors[i];
        num_ports_limit[i] = fixed_factors_limit[i];
      } while (fixed_factors[i] > 0);
    }
    j = -1;
    for (i = 0; i < my_mpi_size_row; i++) {
      if (num_ports[i] > 0) {
        num_ports[i]--;
        num_ports_limit[i]--;
      }
    }
    return (EXT_MPI_Allreduce_native(sendbuf, recvbuf, count, datatype, op,
                                     num_ports, num_ports_limit, num_parallel,
                                     copyin_method, handle));
  } else {
    recvcounts = (int *)malloc(my_mpi_size_row * sizeof(int));
    displs = (int *)malloc(my_mpi_size_row * sizeof(int));
    for (i = 0; i < my_mpi_size_row; i++) {
      recvcounts[i] = count / my_mpi_size_row;
      if (i < count % my_mpi_size_row) {
        recvcounts[i]++;
      }
    }
    retval = EXT_MPI_Reduce_scatter(sendbuf, recvbuf, recvcounts, datatype, op,
                                    handle);
    if (retval != 0) {
      return (retval);
    }
    displs[0] = 0;
    for (i = 0; i < my_mpi_size_row - 1; i++) {
      displs[i + 1] = displs[i] + recvcounts[i];
    }
    retval = EXT_MPI_Allgatherv(recvbuf, recvcounts[my_mpi_rank_row], datatype,
                                recvbuf, recvcounts, displs, datatype, handle);
    free(displs);
    free(recvcounts);
    return (retval);
  }
}

int EXT_MPI_Allocate(MPI_Comm comm_row, int my_cores_per_node_row,
                     MPI_Comm comm_column, int my_cores_per_node_column,
                     int sharedmem_size, int locmem_size_) {
  return (EXT_MPI_Allocate_native(comm_row, my_cores_per_node_row, comm_column,
                                  my_cores_per_node_column, sharedmem_size,
                                  locmem_size_));
}

int EXT_MPI_Dry_allreduce(int mpi_size, int count, int *handle) {
  int *num_ports, *num_ports_limit, *recvcounts;
  int handle1, handle2, i, j;
  if (count * sizeof(long int) <= threshold_rabenseifner) {
    num_ports = (int *)malloc(mpi_size * sizeof(int));
    num_ports_limit = (int *)malloc(mpi_size * sizeof(int));
    if (fixed_factors == NULL) {
      communication_factors(mpi_size, 1 + 1, num_ports, num_ports_limit);
    } else {
      i = -1;
      do {
        i++;
        num_ports[i] = fixed_factors[i];
        num_ports_limit[i] = fixed_factors_limit[i];
      } while (fixed_factors[i] > 0);
    }
    for (i = 0; i < mpi_size; i++) {
      if (num_ports[i] > 0) {
        num_ports[i]--;
        num_ports_limit[i]--;
      }
    }
#ifdef VERBOSE
    printf("# allreduce parameters %d %d %d %d ports ", mpi_size,
           (int)(count * sizeof(long int)), 1, 1);
    i = 0;
    while (num_ports[i] > 0) {
      printf("%d ", num_ports[i]);
      i++;
    }
    printf("ports_limit ");
    i = 0;
    while (num_ports_limit[i] > 0) {
      printf("%d ", num_ports_limit[i]);
      i++;
    }
    printf("\n");
#endif
    *handle = EXT_MPI_Dry_allreduce_native(mpi_size, count, num_ports,
                                           num_ports_limit);
    free(num_ports_limit);
    free(num_ports);
  } else {
    num_ports = (int *)malloc(mpi_size * sizeof(int));
    recvcounts = (int *)malloc(mpi_size * sizeof(int));
    for (i = 0; i < mpi_size; i++) {
      recvcounts[i] = count / mpi_size;
      if (i < count % mpi_size) {
        recvcounts[i]++;
      }
      num_ports[i] = 1;
    }
    handle1 =
        EXT_MPI_Dry_reduce_scatter_native(mpi_size, recvcounts, num_ports);
    handle2 = EXT_MPI_Dry_allgatherv_native(mpi_size, recvcounts, num_ports);
    free(recvcounts);
    free(num_ports);
    *handle = EXT_MPI_Merge_collectives_native(handle1, handle2);
  }
  return (0);
}

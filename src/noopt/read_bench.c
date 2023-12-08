#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "read_bench.h"

FileData *ext_mpi_file_input = NULL;
int ext_mpi_file_input_max = 0, ext_mpi_file_input_max_per_core = 0;

static int bench_selection = -1;
static FileData **file_input = NULL;
static int *file_input_max = NULL, *file_input_max_per_core = NULL;

static void get_file_input_raw_0(int *file_input_max_raw, FileData **file_input_raw) {
#include "latency_bandwidth.tmp"
}

static void get_file_input_raw_1(int *file_input_max_raw, FileData **file_input_raw) {
#include "latency_bandwidth.tmp"
}

static int read_bench_single(int data_set) {
  FileData *file_input_raw = NULL;
  double distance;
  int file_input_max_raw = 0, file_input_max_per_core_raw;
  int cores, i, j;
  switch (data_set) {
    case 0:
      get_file_input_raw_0(&file_input_max_raw, &file_input_raw);
      break;
    case 1:
      get_file_input_raw_1(&file_input_max_raw, &file_input_raw);
      break;
  }
  file_input_max_per_core_raw =
      file_input_max_raw / file_input_raw[file_input_max_raw - 1].nports;
  distance = file_input_raw[1].msize - file_input_raw[0].msize;
  ext_mpi_file_input_max_per_core = (file_input_raw[file_input_max_raw - 1].msize -
                             file_input_raw[0].msize) /
                            distance;
  ext_mpi_file_input_max = ext_mpi_file_input_max_per_core *
                   file_input_raw[file_input_max_raw - 1].nports;
  ext_mpi_file_input = (FileData *)malloc(ext_mpi_file_input_max * sizeof(*ext_mpi_file_input));
  if (!ext_mpi_file_input)
    goto error;
  for (cores = 1; cores <= file_input_raw[file_input_max_raw - 1].nports;
       cores++) {
    j = 0;
    for (i = 0; i < ext_mpi_file_input_max_per_core; i++) {
      for (; file_input_raw[j].msize <
                 i * distance + file_input_raw[0].msize &&
             j < file_input_max_per_core_raw;
           j++) {
      }
      if (j == file_input_max_per_core_raw) {
        ext_mpi_file_input[i + ext_mpi_file_input_max_per_core * (cores - 1)] =
            file_input_raw[j + file_input_max_per_core_raw * (cores - 1)];
      } else {
        if (j == 0) {
          ext_mpi_file_input[i + ext_mpi_file_input_max_per_core * (cores - 1)] =
              file_input_raw[j + file_input_max_per_core_raw * (cores - 1)];
        } else {
          ext_mpi_file_input[i + ext_mpi_file_input_max_per_core * (cores - 1)].nnodes =
              file_input_raw[j + file_input_max_per_core_raw * (cores - 1)]
                  .nnodes;
          ext_mpi_file_input[i + ext_mpi_file_input_max_per_core * (cores - 1)].nports =
              file_input_raw[j + file_input_max_per_core_raw * (cores - 1)]
                  .nports;
          ext_mpi_file_input[i + ext_mpi_file_input_max_per_core * (cores - 1)].parallel =
              file_input_raw[j + file_input_max_per_core_raw * (cores - 1)]
                  .parallel;
          ext_mpi_file_input[i + ext_mpi_file_input_max_per_core * (cores - 1)].msize =
              i * distance + file_input_raw[0].msize;
          ext_mpi_file_input[i + ext_mpi_file_input_max_per_core * (cores - 1)].deltaT =
              file_input_raw[j - 1 +
                             file_input_max_per_core_raw * (cores - 1)]
                  .deltaT +
              (ext_mpi_file_input[i + ext_mpi_file_input_max_per_core * (cores - 1)].msize -
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
  for (i = 0; i < ext_mpi_file_input_max / ext_mpi_file_input_max_per_core; i++) {
    for (j = ext_mpi_file_input_max_per_core - 1 - 1; j >= 0; j--) {
      if (ext_mpi_file_input[j + i * ext_mpi_file_input_max_per_core].deltaT >
          ext_mpi_file_input[(j + 1) + i * ext_mpi_file_input_max_per_core].deltaT) {
        ext_mpi_file_input[j + i * ext_mpi_file_input_max_per_core].deltaT =
            ext_mpi_file_input[(j + 1) + i * ext_mpi_file_input_max_per_core].deltaT;
      }
    }
  }
  return 0;
error:
  return ERROR_MALLOC;
}

static int delete_bench_single() {
  free(ext_mpi_file_input);
  return 0;
}

void ext_mpi_switch_bench(int i) {
  if (bench_selection >= 0) {
    file_input[bench_selection] = ext_mpi_file_input;
    file_input_max[bench_selection] = ext_mpi_file_input_max;
    file_input_max_per_core[bench_selection] = ext_mpi_file_input_max_per_core;
  }
  ext_mpi_file_input = file_input[i];
  ext_mpi_file_input_max = file_input_max[i];
  ext_mpi_file_input_max_per_core = file_input_max_per_core[i];
  bench_selection = i;
}

int ext_mpi_read_bench() {
  int i;
  file_input = (FileData **)malloc(4 * sizeof(FileData *));
  memset(file_input, 0, 4 * sizeof(FileData *));
  file_input_max = (int *)malloc(4 * sizeof(int));
  memset(file_input_max, 0, 4 * sizeof(int));
  file_input_max_per_core = (int *)malloc(4 * sizeof(int));
  memset(file_input_max_per_core, 0, 4 * sizeof(int));
  for (i = 0; i < 2; i++) {
    ext_mpi_switch_bench(i);
    read_bench_single(i);
  }
  return 0;
}

int ext_mpi_delete_bench() {
  int i;
  for (i = 0; i < 2; i++) {
    ext_mpi_switch_bench(i);
    delete_bench_single();
  }
  free(file_input_max_per_core);
  free(file_input_max);
  free(file_input);
  return 0;
}

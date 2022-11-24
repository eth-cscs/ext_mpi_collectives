#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "read_bench.h"

FileData *ext_mpi_file_input = NULL;
int ext_mpi_file_input_max = 0, ext_mpi_file_input_max_per_core = 0;
int ext_mpi_node_size_threshold_max = 0;
int *ext_mpi_node_size_threshold = NULL;

int ext_mpi_read_bench() {
  FileData *file_input_raw = NULL;
  double distance;
  int file_input_max_raw = 0, file_input_max_per_core_raw;
  int cores, i, j;
#include "node_size_threshold.tmp"
#include "latency_bandwidth.tmp"
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

int ext_mpi_delete_bench() {
  free(ext_mpi_file_input);
  free(ext_mpi_node_size_threshold);
  return 0;
}

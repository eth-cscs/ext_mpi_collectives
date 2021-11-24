#ifndef EXT_MPI_READ_BENCH_H_

#define EXT_MPI_READ_BENCH_H_

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct _FileData {
  int nnodes;
  int nports;
  int parallel;
  int msize;
  double deltaT;
} FileData;

extern FileData *ext_mpi_file_input;
extern int ext_mpi_file_input_max, ext_mpi_file_input_max_per_core;
extern int ext_mpi_node_size_threshold_max;
extern int *ext_mpi_node_size_threshold;

int ext_mpi_read_bench();
int ext_mpi_delete_bench();

#ifdef __cplusplus
}
#endif

#endif

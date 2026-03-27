#ifndef EXT_MPI_COUNT_INSTRUCTIONS_H_

#define EXT_MPI_COUNT_INSTRUCTIONS_H_

#ifdef __cplusplus
extern "C"
{
#endif

struct ext_mpi_counters_native {
  int counters_num_memcpy;
  int counters_size_memcpy;
  int counters_num_reduce;
  int counters_size_reduce;
  int counters_num_MPI_Irecv;
  int counters_size_MPI_Irecv;
  int counters_num_MPI_Isend;
  int counters_size_MPI_Isend;
  int counters_num_MPI_Recv;
  int counters_size_MPI_Recv;
  int counters_num_MPI_Send;
  int counters_size_MPI_Send;
  int counters_num_MPI_Sendrecv;
  int counters_size_MPI_Sendrecv;
  int counters_size_MPI_Sendrecvb;
};

int ext_mpi_allreduce_init_draft(void *sendbuf, void *recvbuf, int count,
                                 int type_size,
                                 int mpi_size_row, int my_cores_per_node_row,
                                 int mpi_size_column,
                                 int my_cores_per_node_column, int *num_ports,
                                 int *groups, int num_active_ports,
                                 int bit, char **code_address);

void ext_mpi_get_counters_native(struct ext_mpi_counters_native *var);

void ext_mpi_set_counters_zero_native();

int ext_mpi_simulate_native(char *ip);

int ext_mpi_count_native(char *ip, double *counts, int *num_steps);

#ifdef __cplusplus
}
#endif

#endif

#ifndef EXT_MPI_NATIVE_H_

#define EXT_MPI_NATIVE_H_

#include <mpi.h>
#include "constants.h"
#include "read_write.h"

#ifdef __cplusplus
extern "C" {
#endif

int EXT_MPI_Allreduce_init_native(const void *sendbuf, void *recvbuf, int count,
                                  MPI_Datatype datatype, MPI_Op op,
                                  MPI_Comm comm_row, int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column, int *num_ports,
                                  int *groups, int copyin, int *copyin_factors,
                                  int alt, int bit, int waitany, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem, int *padding_factor, int **mem_partners_send_back, int **mem_partners_recv_back);
int EXT_MPI_Reduce_init_native(const void *sendbuf, void *recvbuf, int count,
                               MPI_Datatype datatype, MPI_Op op, int root,
                               MPI_Comm comm_row, int my_cores_per_node_row,
                               MPI_Comm comm_column,
                               int my_cores_per_node_column, int *num_ports,
                               int *groups, int copyin, int *copyin_factors,
                               int alt, int bit, int waitany, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem, int *padding_factor, int **mem_partners_send_back, int **mem_partners_recv_back);
int EXT_MPI_Bcast_init_native(void *buffer, int count, MPI_Datatype datatype,
                              int root, MPI_Comm comm_row,
                              int my_cores_per_node_row, MPI_Comm comm_column,
                              int my_cores_per_node_column, int *num_ports,
                              int *groups, int copyin, int *copyin_factors,
                              int alt, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem);
int EXT_MPI_Allgatherv_init_native(
    const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    const int *recvcounts, const int *displs, MPI_Datatype recvtype, MPI_Comm comm_row,
    int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *num_ports, int *groups,
    int alt, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem);
int EXT_MPI_Gatherv_init_native(
    const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    const int *recvcounts, const int *displs, MPI_Datatype recvtype, int root,
    MPI_Comm comm_row, int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *num_ports, int *groups,
    int alt, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem);
int EXT_MPI_Reduce_scatter_init_native(
    const void *sendbuf, void *recvbuf, const int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *num_ports,
    int *groups, int copyin, int *copyin_factors, int alt, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem, int *padding_factor);
int EXT_MPI_Scatterv_init_native(const void *sendbuf, const int *sendcounts, const int *displs,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 int recvcount, MPI_Datatype recvtype, int root,
                                 MPI_Comm comm_row, int my_cores_per_node_row,
                                 MPI_Comm comm_column,
                                 int my_cores_per_node_column, int *num_ports,
                                 int *groups,
                                 int copyin, int alt, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem);
int EXT_MPI_Start_native(int handle);
int EXT_MPI_Test_native(int handle);
int EXT_MPI_Wait_native(int handle);
int EXT_MPI_Done_native(int handle);

int EXT_MPI_Init_native();
int EXT_MPI_Initialized_native();
int EXT_MPI_Finalize_native();

void ext_mpi_native_export(int **e_handle_code_max, char ****e_comm_code, char ****e_execution_pointer, int ***e_active_wait, int **e_is_initialised, MPI_Comm **e_EXT_MPI_COMM_WORLD, int **e_tag_max);

#ifdef __cplusplus
}
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static int get_reduction_op(MPI_Datatype datatype, MPI_Op op) {
  if (datatype == MPI_DOUBLE) {
    if (op == MPI_SUM) {
      return OPCODE_REDUCE_SUM_DOUBLE;
    } else if (op == MPI_MAX) {
      return OPCODE_REDUCE_MAX_DOUBLE;
    } else if (op == MPI_MIN) {
      return OPCODE_REDUCE_MIN_DOUBLE;
    } else {
      return OPCODE_REDUCE_USER_DOUBLE;
    }
  } else if (datatype == MPI_FLOAT) {
    if (op == MPI_SUM) {
      return OPCODE_REDUCE_SUM_FLOAT;
    } else if (op == MPI_MAX) {
      return OPCODE_REDUCE_MAX_FLOAT;
    } else if (op == MPI_MIN) {
      return OPCODE_REDUCE_MIN_FLOAT;
    } else {
      return OPCODE_REDUCE_USER_FLOAT;
    }
  } else if (datatype == MPI_INT) {
    if (op == MPI_SUM) {
      return OPCODE_REDUCE_SUM_INT;
    } else if (op == MPI_MAX) {
      return OPCODE_REDUCE_MAX_INT;
    } else if (op == MPI_MIN) {
      return OPCODE_REDUCE_MIN_INT;
    } else {
      return OPCODE_REDUCE_USER_INT;
    }
  } else if (datatype == MPI_LONG) {
    if (op == MPI_SUM) {
      return OPCODE_REDUCE_SUM_LONG_INT;
    } else if (op == MPI_MAX) {
      return OPCODE_REDUCE_MAX_LONG_INT;
    } else if (op == MPI_MIN) {
      return OPCODE_REDUCE_MIN_LONG_INT;
    } else {
      return OPCODE_REDUCE_USER_LONG_INT;
    }
  } else {
    return -1;
  }
}
#pragma GCC diagnostic pop
#endif

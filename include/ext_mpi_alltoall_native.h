#ifndef EXT_MPI_ALLTOALL_NATIVE_H_

#define EXT_MPI_ALLTOALL_NATIVE_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

struct EXT_MPI_Variables_native {
  int shmemid;
  char *shmem;
  int shmem_size;
  MPI_Comm shmem_comm_node_row;
  MPI_Comm shmem_comm_node_column;
  char *locmem;
  int locmem_size;
  int handle_code_max;
  int barrier_count;
  char **comm_code;
};
void EXT_MPI_Get_variables_native(struct EXT_MPI_Variables_native *var);
void EXT_MPI_Set_variables_native(struct EXT_MPI_Variables_native *var);

struct EXT_MPI_Counters_native {
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
void EXT_MPI_Get_counters_native(struct EXT_MPI_Counters_native *var);
void EXT_MPI_Set_counters_zero_native();

int EXT_MPI_Alltoall_init_native(void *sendbuf, int sendcount,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 int recvcount, MPI_Datatype recvtype,
                                 MPI_Comm comm_row, int my_cores_per_node_row,
                                 MPI_Comm comm_column,
                                 int my_cores_per_node_column, int num_ports,
                                 int num_active_ports, int chunks_throttle);
int EXT_MPI_Alltoallv_init_native(
    void *sendbuf, int *sendcounts, int *sdispls, MPI_Datatype sendtype,
    void *recvbuf, int *recvcounts, int *rdispls, MPI_Datatype recvtype,
    MPI_Comm comm_row, int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int num_active_ports, int chunks_throttle);
void EXT_MPI_Ialltoall_init_native(void *sendbuf, int sendcount,
                                   MPI_Datatype sendtype, void *recvbuf,
                                   int recvcount, MPI_Datatype recvtype,
                                   MPI_Comm comm_row, int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column,
                                   int num_active_ports, int *handle_begin,
                                   int *handle_wait);
void EXT_MPI_Ialltoallv_init_native(
    void *sendbuf, int *sendcounts, int *sdispls, MPI_Datatype sendtype,
    void *recvbuf, int *recvcounts, int *rdispls, MPI_Datatype recvtype,
    MPI_Comm comm_row, int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int num_active_ports, int *handle_begin,
    int *handle_wait);
int EXT_MPI_Allgatherv_init_native(
    void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    int *recvcounts, int *displs, MPI_Datatype recvtype, MPI_Comm comm_row,
    int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *num_ports, int *num_parallel,
    int num_active_ports, int allreduce, int bruck, int recvcount);
int EXT_MPI_Reduce_scatter_init_native(
    void *sendbuf, void *recvbuf, int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *num_ports,
    int *num_parallel, int num_active_ports, int copyin, int allreduce,
    int bruck, int recvcount);
int EXT_MPI_Allreduce_init_native(void *sendbuf, void *recvbuf, int count,
                                  MPI_Datatype datatype, MPI_Op op,
                                  MPI_Comm comm_row, int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column, int *num_ports,
                                  int *num_ports_limit, int num_active_ports,
                                  int copyin);
int EXT_MPI_Bcast_init_native(void *sendbuf, int count, MPI_Datatype datatype,
                              int root, MPI_Comm comm_row,
                              int my_cores_per_node_row, MPI_Comm comm_column,
                              int my_cores_per_node_column, int *num_ports,
                              int *num_parallel, int num_active_ports,
                              int bruck);
int EXT_MPI_Reduce_init_native(void *sendbuf, void *recvbuf, int count,
                               MPI_Datatype datatype, MPI_Op op, int root,
                               MPI_Comm comm_row, int my_cores_per_node_row,
                               MPI_Comm comm_column,
                               int my_cores_per_node_column, int *num_ports,
                               int *num_parallel, int num_active_ports,
                               int bruck);
int EXT_MPI_Merge_collectives_native(int handle1, int handle2);
int EXT_MPI_Exec_native(int handle);
int EXT_MPI_Done_native(int handle);
int EXT_MPI_Allocate_native(MPI_Comm comm_row, int my_cores_per_node_row,
                            MPI_Comm comm_column, int my_cores_per_node_column,
                            int sharedmem_size, int locmem_size_);
int EXT_MPI_Get_size_rank_native(int handle, int *my_cores_per_node_row,
                                 int *my_cores_per_node_column,
                                 int *my_mpi_size_row, int *my_mpi_rank_row);
int EXT_MPI_Init_general_native(MPI_Comm comm_row, int my_cores_per_node_row,
                                MPI_Comm comm_column,
                                int my_cores_per_node_column);
int EXT_MPI_Done_general_native(int handle);
int EXT_MPI_Allgatherv_native(void *sendbuf, int sendcount,
                              MPI_Datatype sendtype, void *recvbuf,
                              int *recvcounts, int *displs,
                              MPI_Datatype recvtype, int *num_ports,
                              int *num_parallel, int handle);
int EXT_MPI_Allgather_native(void *sendbuf, int sendcount,
                             MPI_Datatype sendtype, void *recvbuf,
                             int recvcount, MPI_Datatype recvtype,
                             int *num_ports, int *num_parallel, int handle);
int EXT_MPI_Reduce_scatter_native(void *sendbuf, void *recvbuf, int *recvcounts,
                                  MPI_Datatype datatype, MPI_Op op,
                                  int *num_ports, int *num_parallel, int copyin,
                                  int handle);
int EXT_MPI_Reduce_scatter_block_native(void *sendbuf, void *recvbuf,
                                        int recvcount, MPI_Datatype datatype,
                                        MPI_Op op, int *num_ports,
                                        int *num_parallel, int copyin,
                                        int handle);
int EXT_MPI_Allreduce_native(void *sendbuf, void *recvbuf, int count,
                             MPI_Datatype datatype, MPI_Op op, int *num_ports,
                             int *num_ports_limit, int *num_parallel,
                             int copyin, int handle);
int EXT_MPI_Bcast_native(void *sendbuf, int count, MPI_Datatype datatype,
                         int root, int *num_ports, int *num_parallel,
                         int handle);
int EXT_MPI_Reduce_native(void *sendbuf, void *recvbuf, int count,
                          MPI_Datatype datatype, MPI_Op op, int root,
                          int *num_ports, int *num_parallel, int handle);
int EXT_MPI_Simulate_native(int handle);
int EXT_MPI_Dry_allgatherv_native(int mpi_size, int *counts, int *num_ports);
int EXT_MPI_Dry_reduce_scatter_native(int mpi_size, int *counts,
                                      int *num_ports);
int EXT_MPI_Dry_allreduce_native(int mpi_size, int count, int *num_ports,
                                 int *num_ports_limit);
int EXT_MPI_Allgatherv_init_native_big(void *sendbuf, int sendcount,
                                       MPI_Datatype sendtype, void *recvbuf,
                                       int *recvcounts, int *displs,
                                       MPI_Datatype recvtype, MPI_Comm comm);
int EXT_MPI_Reduce_scatter_init_native_big(void *sendbuf, void *recvbuf,
                                           int *recvcounts, int *displs,
                                           MPI_Datatype datatype, MPI_Comm comm,
                                           MPI_Op op);

#ifdef __cplusplus
}
#endif

#endif

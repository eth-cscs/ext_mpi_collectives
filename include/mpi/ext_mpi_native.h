#ifndef EXT_MPI_NATIVE_H_

#define EXT_MPI_NATIVE_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

int EXT_MPI_Allreduce_init_native(void *sendbuf, void *recvbuf, int count,
                                  MPI_Datatype datatype, MPI_Op op,
                                  MPI_Comm comm_row, int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column, int *num_ports,
                                  int *groups, int num_active_ports, int copyin,
                                  int alt, int bit,int waitany);
int EXT_MPI_Reduce_init_native(void *sendbuf, void *recvbuf, int count,
                               MPI_Datatype datatype, MPI_Op op, int root,
                               MPI_Comm comm_row, int my_cores_per_node_row,
                               MPI_Comm comm_column,
                               int my_cores_per_node_column, int *num_ports,
                               int *groups, int num_active_ports, int copyin,
                               int alt, int bit,int waitany);
int EXT_MPI_Bcast_init_native(void *buffer, int count, MPI_Datatype datatype,
                              int root, MPI_Comm comm_row,
                              int my_cores_per_node_row, MPI_Comm comm_column,
                              int my_cores_per_node_column, int *num_ports,
                              int *groups, int num_active_ports, int copyin,
                              int alt);
int EXT_MPI_Allgatherv_init_native(
    void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    int *recvcounts, int *displs, MPI_Datatype recvtype, MPI_Comm comm_row,
    int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *num_ports, int *num_parallel,
    int num_active_ports, int alt);
int EXT_MPI_Gatherv_init_native(
    void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    int *recvcounts, int *displs, MPI_Datatype recvtype, int root,
    MPI_Comm comm_row, int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *num_ports, int *num_parallel,
    int num_active_ports, int alt);
int EXT_MPI_Reduce_scatter_init_native(
    void *sendbuf, void *recvbuf, int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *num_ports,
    int *num_parallel, int num_active_ports, int copyin, int alt);
int EXT_MPI_Scatterv_init_native(void *sendbuf, int *sendcounts, int *displs,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 int recvcount, MPI_Datatype recvtype, int root,
                                 MPI_Comm comm_row, int my_cores_per_node_row,
                                 MPI_Comm comm_column,
                                 int my_cores_per_node_column, int *num_ports,
                                 int *num_parallel, int num_active_ports,
                                 int copyin, int alt);
int EXT_MPI_Exec_native(int handle);
int EXT_MPI_Test_native(int handle);
int EXT_MPI_Progress_native();
int EXT_MPI_Wait_native(int handle);
int EXT_MPI_Done_native(int handle);

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

int EXT_MPI_Simulate_native(int handle);
int EXT_MPI_Count_native(int handle, double *counts, int *num_steps);

int EXT_MPI_Allreduce_init_draft(void *sendbuf, void *recvbuf, int count,
                                 MPI_Datatype datatype, MPI_Op op,
                                 int mpi_size_row, int my_cores_per_node_row,
                                 int mpi_size_column,
                                 int my_cores_per_node_column, int *num_ports,
                                 int *groups, int num_active_ports, int copyin,
                                 int bit);

int EXT_MPI_Init_native();
int EXT_MPI_Initialized_native();
int EXT_MPI_Finalize_native();

#ifdef __cplusplus
}
#endif

#endif

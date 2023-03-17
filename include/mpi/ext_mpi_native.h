#ifndef EXT_MPI_NATIVE_H_

#define EXT_MPI_NATIVE_H_

#include <mpi.h>
#include "read_write.h"

#ifdef __cplusplus
extern "C" {
#endif

int EXT_MPI_Allreduce_init_native(const void *sendbuf, void *recvbuf, int count,
                                  MPI_Datatype datatype, MPI_Op op,
                                  MPI_Comm comm_row, int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column, int *num_ports,
                                  int *groups, int num_active_ports, int copyin, int *copyin_factors,
                                  int alt, int bit, int waitany, int recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem);
int EXT_MPI_Reduce_init_native(const void *sendbuf, void *recvbuf, int count,
                               MPI_Datatype datatype, MPI_Op op, int root,
                               MPI_Comm comm_row, int my_cores_per_node_row,
                               MPI_Comm comm_column,
                               int my_cores_per_node_column, int *num_ports,
                               int *groups, int num_active_ports, int copyin, int *copyin_factors,
                               int alt, int bit, int waitany, int recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem);
int EXT_MPI_Bcast_init_native(void *buffer, int count, MPI_Datatype datatype,
                              int root, MPI_Comm comm_row,
                              int my_cores_per_node_row, MPI_Comm comm_column,
                              int my_cores_per_node_column, int *num_ports,
                              int *groups, int num_active_ports, int copyin, int *copyin_factors,
                              int alt, int recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem);
int EXT_MPI_Allgatherv_init_native(
    const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    const int *recvcounts, const int *displs, MPI_Datatype recvtype, MPI_Comm comm_row,
    int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *num_ports, int *groups,
    int num_active_ports, int alt, int recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem);
int EXT_MPI_Gatherv_init_native(
    const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    const int *recvcounts, const int *displs, MPI_Datatype recvtype, int root,
    MPI_Comm comm_row, int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *num_ports, int *groups,
    int num_active_ports, int alt, int recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem);
int EXT_MPI_Reduce_scatter_init_native(
    const void *sendbuf, void *recvbuf, const int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *num_ports,
    int *groups, int num_active_ports, int copyin, int *copyin_factors, int alt, int recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem);
int EXT_MPI_Scatterv_init_native(const void *sendbuf, const int *sendcounts, const int *displs,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 int recvcount, MPI_Datatype recvtype, int root,
                                 MPI_Comm comm_row, int my_cores_per_node_row,
                                 MPI_Comm comm_column,
                                 int my_cores_per_node_column, int *num_ports,
                                 int *groups, int num_active_ports,
                                 int copyin, int alt, int recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem);
int EXT_MPI_Start_native(int handle);
int EXT_MPI_Test_native(int handle);
int EXT_MPI_Progress_native();
int EXT_MPI_Wait_native(int handle);
int EXT_MPI_Done_native(int handle);

int EXT_MPI_Init_native();
int EXT_MPI_Initialized_native();
int EXT_MPI_Finalize_native();

int EXT_MPI_Add_blocking_native(int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, int my_cores_per_node, int *num_ports, int *groups, int copyin, int *copyin_factors, int alt, int bit, int recursive, int blocking, int num_sockets_per_node, enum ecollective_type collective_type);
int EXT_MPI_Release_blocking_native(int i_comm);
int EXT_MPI_Allreduce_native(const void *sendbuf, void *recvbuf, int count, int reduction_op, MPI_Comm comm);

#ifdef __cplusplus
}
#endif

#endif

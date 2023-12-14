#ifndef EXT_MPI_NATIVE_EXEC_H_

#define EXT_MPI_NATIVE_EXEC_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

int ext_mpi_exec_native(char *ip, char **ip_exec, int active_wait);
int ext_mpi_exec_blocking(char *ip, MPI_Comm comm, int tag, char *shmem_socket, int *counter_socket, int socket_rank, int num_cores, char **shmem_node, int *counter_node, int num_sockets_per_node, void **shmem_blocking, const void *sendbuf, void *recvbuf, int count, int reduction_op, int count_io, char **send_pointers, void *p_dev_temp);
int ext_mpi_normalize_blocking(char *ip, MPI_Comm comm, int tag, int count, char **send_pointers_allreduce_blocking);
int ext_mpi_exec_padding(char *ip, void *sendbuf, void *recvbuf, void **shmem, int *numbers_padding);

#ifdef __cplusplus
}
#endif

#endif

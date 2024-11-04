#ifndef EXT_MPI_XPMEM_H_

#define EXT_MPI_XPMEM_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef XPMEM
int ext_mpi_init_xpmem(MPI_Comm comm);
int ext_mpi_sendrecvbuf_init_xpmem(MPI_Comm comm, int my_cores_per_node, int num_sockets, char *sendrecvbuf, int size, char ***sendrecvbufs);
int ext_mpi_sendrecvbuf_done_xpmem(MPI_Comm comm, int my_cores_per_node, char **sendrecvbufs);
int ext_mpi_sendrecvbuf_init_xpmem_blocking(int my_mpi_rank, int my_cores_per_node, int num_sockets, char *sendbuf, char *recvbuf, size_t size, int *mem_partners, char ***shmem, int **shmem_node, int *counter, char **sendbufs, char **recvbufs);
int ext_mpi_sendrecvbuf_done_xpmem_blocking(char **sendbufs, char **recvbufs, int *mem_partners);
#endif

#ifdef __cplusplus
}
#endif

#endif

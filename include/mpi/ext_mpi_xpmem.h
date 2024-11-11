#ifndef EXT_MPI_XPMEM_H_

#define EXT_MPI_XPMEM_H_

#include <mpi.h>

#ifdef XPMEM
struct xpmem_tree {
  struct xpmem_tree *left, *right, *next;
  unsigned long int offset;
  size_t size;
  char *a;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef XPMEM
int ext_mpi_init_xpmem(MPI_Comm comm);
void ext_mpi_done_xpmem();
int ext_mpi_sendrecvbuf_init_xpmem(MPI_Comm comm_world, MPI_Comm comm, int my_cores_per_node, int num_sockets, char *sendrecvbuf, int size, char ***sendrecvbufs);
int ext_mpi_sendrecvbuf_done_xpmem(MPI_Comm comm, int my_cores_per_node, char **sendrecvbufs);
int ext_mpi_init_xpmem_blocking(MPI_Comm comm_world, MPI_Comm comm, int num_sockets_per_node, long long int **all_xpmem_id_permutated, struct xpmem_tree ***xpmem_tree_root);
void ext_mpi_done_xpmem_blocking(MPI_Comm comm, struct xpmem_tree **xpmem_tree_root);
int ext_mpi_sendrecvbuf_init_xpmem_blocking(struct xpmem_tree **xpmem_tree_root, int my_mpi_rank, int my_cores_per_node, int num_sockets, char *sendbuf, char *recvbuf, size_t size, long long int *all_xpmem_id_permutated, int *mem_partners_send, int *mem_partners_recv, char ***shmem, int **shmem_node, int *counter, char **sendbufs, char **recvbufs);
int ext_mpi_sendrecvbuf_done_xpmem_blocking(char **sendbufs, char **recvbufs, int *mem_partners_send, int *mem_partners_recv);
#endif

#ifdef __cplusplus
}
#endif

#endif

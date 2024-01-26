#ifndef EXT_MPI_XPMEM_H_

#define EXT_MPI_XPMEM_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef XPMEM
int ext_mpi_init_xpmem(MPI_Comm comm);
int ext_mpi_sendrecvbuf_init_xpmem(MPI_Comm comm, int my_cores_per_node, char *sendrecvbuf, int size, char ***sendrecvbufs);
int ext_mpi_sendrecvbuf_done_xpmem(MPI_Comm comm, int my_cores_per_node, char **sendrecvbufs);
#endif

#ifdef __cplusplus
}
#endif

#endif

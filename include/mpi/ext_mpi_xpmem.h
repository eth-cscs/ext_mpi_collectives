#ifndef EXT_MPI_XPMEM_H_

#define EXT_MPI_XPMEM_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef XPMEM
int ext_mpi_init_xpmem(MPI_Comm comm);
#endif

#ifdef __cplusplus
}
#endif

#endif

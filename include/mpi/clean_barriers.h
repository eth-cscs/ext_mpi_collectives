#ifndef EXT_MPI_CLEAN_BARRIERS_H_

#define EXT_MPI_CLEAN_BARRIERS_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

int ext_mpi_clean_barriers(char *buffer_in, char *buffer_out, MPI_Comm comm_row,
                           MPI_Comm comm_column);

#ifdef __cplusplus
}
#endif

#endif

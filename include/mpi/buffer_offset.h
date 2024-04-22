#ifndef EXT_MPI_BUFFER_OFFSET_H_

#define EXT_MPI_BUFFER_OFFSET_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C"
{
#endif

int ext_mpi_generate_buffer_offset(char *buffer_in, char *buffer_out, MPI_Comm *comm);

#ifdef __cplusplus
}
#endif

#endif

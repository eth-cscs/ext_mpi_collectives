#ifndef EXT_MPI_FORWARD_INTERPRETER_H_

#define EXT_MPI_FORWARD_INTERPRETER_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

int ext_mpi_generate_forward_interpreter(char *buffer_in, char *buffer_out,
                                         MPI_Comm comm_row);

#ifdef __cplusplus
}
#endif

#endif

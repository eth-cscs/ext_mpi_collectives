#ifndef EXT_MPI_PADDING_FACTOR_H_

#define EXT_MPI_PADDING_FACTOR_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

int ext_mpi_padding_factor(int number, MPI_Comm comm);

#ifdef __cplusplus
}
#endif

#endif

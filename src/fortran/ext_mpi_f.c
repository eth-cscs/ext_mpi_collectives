#include "ext_mpi_interface.h"
#include <stdlib.h>
#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

void mpi_init_(int *ierr){ *ierr = MPI_Init(NULL, NULL); }
void mpi_finalize_(int *ierr){ *ierr = MPI_Finalize(); }

void mpi_allreduce_init_(const void *sendbuf, void *recvbuf, int *count, MPI_Datatype *datatype, MPI_Op *op, MPI_Comm *comm, MPI_Info *info, MPI_Request *request, int *ierr){
  *ierr = MPI_Allreduce_init(sendbuf, recvbuf, *count, *datatype, *op, *comm, *info, request);
}

void mpi_request_free_(MPI_Request *request, int *ierr){ *ierr = MPI_Request_free(request); }
void mpi_start_(MPI_Request *request, int *ierr){ *ierr = MPI_Start(request); }
void mpi_wait_(MPI_Request *request, MPI_Status *status, int *ierr){ *ierr = MPI_Wait(request, status); }

#ifdef __cplusplus
}
#endif

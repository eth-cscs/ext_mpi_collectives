#include "ext_mpi_interface.h"
#include <stdlib.h>
#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int MPIR_F08_MPI_IN_PLACE;
extern int ext_mpi_is_blocking;

void mpi_init_f08_(int *ierr){ *ierr = MPI_Init(NULL, NULL); }
void mpi_finalize_f08_(int *ierr){ *ierr = MPI_Finalize(); }

void mpi_allreduce_init_f08_(const void *sendbuf, void *recvbuf, int *count, MPI_Datatype *datatype, MPI_Op *op, MPI_Comm *comm, MPI_Info *info, MPI_Request *request, int *ierr){
  MPI_Datatype my_data_type;
  if (sendbuf == &MPIR_F08_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  if (*datatype == MPI_REAL) {
    my_data_type = MPI_FLOAT;
  } else if (*datatype == MPI_DOUBLE_PRECISION) {
    my_data_type = MPI_DOUBLE;
  } else if (*datatype == MPI_REAL4) {
    my_data_type = MPI_FLOAT;
  } else if (*datatype == MPI_REAL8) {
    my_data_type = MPI_DOUBLE;
  } else if (*datatype == MPI_INTEGER) {
    my_data_type = MPI_INT;
  } else if (*datatype == MPI_INTEGER4) {
    my_data_type = MPI_INT;
  } else if (*datatype == MPI_INTEGER8) {
    my_data_type = MPI_LONG_INT;
  } else {
    my_data_type = *datatype;
  }
  *ierr = MPI_Allreduce_init(sendbuf, recvbuf, *count, my_data_type, *op, *comm, *info, request);
}

void mpi_request_free_f08_(MPI_Request *request, int *ierr){ *ierr = MPI_Request_free(request); }
void mpi_start_f08_(MPI_Request *request, int *ierr){ *ierr = MPI_Start(request); }
void mpi_wait_f08_(MPI_Request *request, MPI_Status *status, int *ierr){ *ierr = MPI_Wait(request, status); }

/*void pmpi_allreduce_f08_(const void *sendbuf, void *recvbuf, int *count, MPI_Datatype *datatype, MPI_Op *op, MPI_Comm *comm, int *ierr);*/
void mpi_allreduce_f08_(const void *sendbuf, void *recvbuf, int *count, MPI_Datatype *datatype, MPI_Op *op, MPI_Comm *comm, int *ierr){
  MPI_Datatype my_data_type;
  if (!ext_mpi_is_blocking) {
/*    pmpi_allreduce_f08_(sendbuf, recvbuf, count, datatype, op, comm, ierr);
    return;*/
  }
  if (sendbuf == &MPIR_F08_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  if (*datatype == MPI_REAL) {
    my_data_type = MPI_FLOAT;
  } else if (*datatype == MPI_DOUBLE_PRECISION) {
    my_data_type = MPI_DOUBLE;
  } else if (*datatype == MPI_REAL4) {
    my_data_type = MPI_FLOAT;
  } else if (*datatype == MPI_REAL8) {
    my_data_type = MPI_DOUBLE;
  } else if (*datatype == MPI_INTEGER) {
    my_data_type = MPI_INT;
  } else if (*datatype == MPI_INTEGER4) {
    my_data_type = MPI_INT;
  } else if (*datatype == MPI_INTEGER8) {
    my_data_type = MPI_LONG_INT;
  } else {
    my_data_type = *datatype;
  }
  *ierr = MPI_Allreduce(sendbuf, recvbuf, *count, my_data_type, *op, *comm);
}

void mpi_comm_dup_f08_(MPI_Comm *comm, MPI_Comm *newcomm, int *ierr){ *ierr = MPI_Comm_dup(*comm, newcomm); }
void mpi_comm_create_f08_(MPI_Comm *comm, MPI_Group *group, MPI_Comm *newcomm, int *ierr){ *ierr = MPI_Comm_create(*comm, *group, newcomm); }
void mpi_comm_split_f08_(MPI_Comm *comm, int *color, int *key, MPI_Comm *newcomm, int *ierr){ *ierr = MPI_Comm_split(*comm, *color, *key, newcomm); }
void mpi_comm_split_type_f08_(MPI_Comm *comm, int *split_type, int *key, MPI_Info *info, MPI_Comm *newcomm, int *ierr){ *ierr = MPI_Comm_split_type(*comm, *split_type, *key, *info, newcomm); }
void mpi_comm_free_f08_(MPI_Comm *comm, int *ierr){ *ierr = MPI_Comm_free(comm); }

void mpi_allreduce_init_(const void *sendbuf, void *recvbuf, int *count, MPI_Datatype *datatype, MPI_Op *op, MPI_Comm *comm, MPI_Info *info, MPI_Request *request, int *ierr){
  mpi_allreduce_init_f08_(sendbuf, recvbuf, count, datatype, op, comm, info, request, ierr);
}

#ifdef __cplusplus
}
#endif

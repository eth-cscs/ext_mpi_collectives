#include "ext_mpi_interface.h"
#include <stdlib.h>
#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int MPIR_F08_MPI_IN_PLACE;
extern void *MPIR_F_MPI_IN_PLACE;
extern void *MPIR_F_MPI_BOTTOM;
extern int ext_mpi_is_blocking;

void mpi_init_f08_(int *ierr){ *ierr = MPI_Init(NULL, NULL); }
void mpi_init_thread_f08_(int *required, int *provided, int *ierr){ *ierr = MPI_Init_thread(NULL, NULL, *required, provided); }
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
void mpi_comm_create_group_f08_(MPI_Comm *comm, MPI_Group *group, int *tag, MPI_Comm *newcomm, int *ierr){ *ierr = MPI_Comm_create_group(*comm, *group, *tag, newcomm); }
void mpi_comm_split_f08_(MPI_Comm *comm, int *color, int *key, MPI_Comm *newcomm, int *ierr){ *ierr = MPI_Comm_split(*comm, *color, *key, newcomm); }
void mpi_comm_split_type_f08_(MPI_Comm *comm, int *split_type, int *key, MPI_Info *info, MPI_Comm *newcomm, int *ierr){ *ierr = MPI_Comm_split_type(*comm, *split_type, *key, *info, newcomm); }
void mpi_comm_free_f08_(MPI_Comm *comm, int *ierr){ *ierr = MPI_Comm_free(comm); }

void mpi_comm_dup_f08ts_(MPI_Comm *comm, MPI_Comm *newcomm, int *ierr){ *ierr = MPI_Comm_dup(*comm, newcomm); }
void mpi_comm_create_f08ts_(MPI_Comm *comm, MPI_Group *group, MPI_Comm *newcomm, int *ierr){ *ierr = MPI_Comm_create(*comm, *group, newcomm); }
void mpi_comm_create_group_f08ts_(MPI_Comm *comm, MPI_Group *group, int *tag, MPI_Comm *newcomm, int *ierr){ *ierr = MPI_Comm_create_group(*comm, *group, *tag, newcomm); }
void mpi_comm_split_f08ts_(MPI_Comm *comm, int *color, int *key, MPI_Comm *newcomm, int *ierr){ *ierr = MPI_Comm_split(*comm, *color, *key, newcomm); }
void mpi_comm_split_type_f08ts_(MPI_Comm *comm, int *split_type, int *key, MPI_Info *info, MPI_Comm *newcomm, int *ierr){ *ierr = MPI_Comm_split_type(*comm, *split_type, *key, *info, newcomm); }
void mpi_comm_free_f08ts_(MPI_Comm *comm, int *ierr){ *ierr = MPI_Comm_free(comm); }

/*void mpi_allreduce_init_(const void *sendbuf, void *recvbuf, int *count, MPI_Datatype *datatype, MPI_Op *op, MPI_Comm *comm, MPI_Info *info, MPI_Request *request, int *ierr){
  mpi_allreduce_init_f08_(sendbuf, recvbuf, count, datatype, op, comm, info, request, ierr);
}*/

void mpi_allreduce_f08ts_(const void *sendbuf, void *recvbuf, int *count, MPI_Datatype *datatype, MPI_Op *op, MPI_Comm *comm, int *ierr){
//	printf("mpi_allreduce_f08ts_\n");
  mpi_allreduce_f08_(sendbuf, recvbuf, count, datatype, op, comm, ierr);
}

void mpi_allreduce_(const void *sendbuf, void *recvbuf, int *count, int *datatype, int *op, int *comm, int *ierr) {
//	printf("mpi_allreduce_\n");
//  if (sendbuf == &MPIR_F08_MPI_IN_PLACE) {
//    sendbuf = MPI_IN_PLACE;
//  }
//  *ierr = MPI_Allreduce(sendbuf, recvbuf, *count, MPI_Type_f2c(*datatype), MPI_Op_f2c(*op), MPI_Comm_f2c(*comm));
    if (sendbuf == MPIR_F_MPI_BOTTOM) {
        sendbuf = MPI_BOTTOM;
    } else if (sendbuf == MPIR_F_MPI_IN_PLACE) {
        sendbuf = MPI_IN_PLACE;
    }

    if (recvbuf == MPIR_F_MPI_BOTTOM) {
        recvbuf = MPI_BOTTOM;
    }

    *ierr = MPI_Allreduce(sendbuf, recvbuf, (int) (*count), (MPI_Datatype) (*datatype), (MPI_Op) (*op), (MPI_Comm) (*comm));
}

void mpi_allreduce__(const void *sendbuf, void *recvbuf, int *count, int *datatype, int *op, int *comm, int *ierr) {
//	printf("mpi_allreduce__\n");
  mpi_allreduce_(sendbuf, recvbuf, count, datatype, op, comm, ierr);
}

void mpi_allreduce(const void *sendbuf, void *recvbuf, int *count, int *datatype, int *op, int *comm, int *ierr) {
//	printf("mpi_allreduce\n");
  mpi_allreduce_(sendbuf, recvbuf, count, datatype, op, comm, ierr);
}

void MPI_ALLREDUCE(const void *sendbuf, void *recvbuf, int *count, int *datatype, int *op, int *comm, int *ierr) {
//	printf("MPI_ALLREDUCE\n");
  mpi_allreduce_(sendbuf, recvbuf, count, datatype, op, comm, ierr);
}

void mpi_init_(int *ierr){
  mpi_init_f08_(ierr);
}

void mpi_init_thread_(int *required, int *provided, int *ierr){
  mpi_init_thread_f08_(required, provided, ierr);
}

void mpi_finalize_(int *ierr){
  mpi_finalize_f08_(ierr);
}

void mpi_comm_dup_(int *comm, int *newcomm, int *ierr){
//  MPI_Comm ncomml, comml = MPI_Comm_f2c(*comm); *ierr = MPI_Comm_dup(comml, &ncomml); *newcomm = MPI_Comm_c2f(ncomml);
  *ierr = MPI_Comm_dup((MPI_Comm) (*comm), (MPI_Comm *) newcomm);
}
void mpi_comm_create_(int *comm, int *group, int *newcomm, int *ierr){
//  MPI_Group groupl; MPI_Comm ncomml, comml = MPI_Comm_f2c(*comm); groupl = MPI_Group_f2c(*group); *ierr = MPI_Comm_create(comml, groupl, &ncomml); *newcomm = MPI_Comm_c2f(ncomml);
  *ierr = MPI_Comm_create((MPI_Comm) (*comm), (MPI_Group) (*group), (MPI_Comm *) newcomm);
}
void mpi_comm_create_group_(int *comm, int *group, int *tag, int *newcomm, int *ierr){
//  MPI_Group groupl; MPI_Comm ncomml, comml = MPI_Comm_f2c(*comm); groupl = MPI_Group_f2c(*group); *ierr = MPI_Comm_create(comml, groupl, &ncomml); *newcomm = MPI_Comm_c2f(ncomml);
  *ierr = MPI_Comm_create_group((MPI_Comm) (*comm), (MPI_Group) (*group), (int) (*tag), (MPI_Comm *) newcomm);
}
void mpi_comm_split_(int *comm, int *color, int *key, int *newcomm, int *ierr){
//  MPI_Comm ncomml, comml = MPI_Comm_f2c(*comm); *ierr = MPI_Comm_split(comml, *color, *key, &ncomml); *newcomm = MPI_Comm_c2f(ncomml);
  *ierr = MPI_Comm_split((MPI_Comm) (*comm), (int) (*color), (int) (*key), (MPI_Comm *) newcomm);
}
void mpi_comm_split_type_(int *comm, int *split_type, int *key, int *info, int *newcomm, int *ierr){
//  MPI_Info infol; MPI_Comm ncomml, comml = MPI_Comm_f2c(*comm); infol = MPI_Info_f2c(*info); *ierr = MPI_Comm_split_type(comml, *split_type, *key, infol, &ncomml); *newcomm = MPI_Comm_c2f(ncomml);
  *ierr = MPI_Comm_split_type((MPI_Comm) (*comm), (int) (*split_type), (int) (*key), (MPI_Info) (*info), (MPI_Comm *) newcomm);
}
void mpi_comm_free_(int *comm, int *ierr){
//  MPI_Comm comml = MPI_Comm_f2c(*comm); *ierr = MPI_Comm_free(&comml);
  *ierr = MPI_Comm_free((MPI_Comm *) comm);
}

void mpi_comm_dup(int *comm, int *newcomm, int *ierr){ mpi_comm_dup_(comm, newcomm, ierr); }
void mpi_comm_create(int *comm, int *group, int *newcomm, int *ierr){ mpi_comm_create_(comm, group, newcomm, ierr); }
void mpi_comm_create_group(int *comm, int *group, int *tag, int *newcomm, int *ierr) {mpi_comm_create_group_(comm, group, tag, newcomm, ierr); }
void mpi_comm_split(int *comm, int *color, int *key, int *newcomm, int *ierr){ mpi_comm_split_(comm, color, key, newcomm, ierr); }
void mpi_comm_split_type(int *comm, int *split_type, int *key, int *info, int *newcomm, int *ierr){ mpi_comm_split_type_(comm, split_type, key, info, newcomm, ierr); }
void mpi_comm_free(int *comm, int *ierr){ mpi_comm_free_(comm, ierr); }

void mpi_comm_dup__(int *comm, int *newcomm, int *ierr){ mpi_comm_dup_(comm, newcomm, ierr); }
void mpi_comm_create__(int *comm, int *group, int *newcomm, int *ierr){ mpi_comm_create_(comm, group, newcomm, ierr); }
void mpi_comm_create_group__(int *comm, int *group, int *tag, int *newcomm, int *ierr) {mpi_comm_create_group_(comm, group, tag, newcomm, ierr); }
void mpi_comm_split__(int *comm, int *color, int *key, int *newcomm, int *ierr){ mpi_comm_split_(comm, color, key, newcomm, ierr); }
void mpi_comm_split_type__(int *comm, int *split_type, int *key, int *info, int *newcomm, int *ierr){ mpi_comm_split_type_(comm, split_type, key, info, newcomm, ierr); }
void mpi_comm_free__(int *comm, int *ierr){ mpi_comm_free_(comm, ierr); }

void MPI_COMM_DUP(int *comm, int *newcomm, int *ierr){ mpi_comm_dup_(comm, newcomm, ierr); }
void MPI_COMM_CREATE(int *comm, int *group, int *newcomm, int *ierr){ mpi_comm_create_(comm, group, newcomm, ierr); }
void MPI_COMM_CREATE_GROUP(int *comm, int *group, int *tag, int *newcomm, int *ierr) {mpi_comm_create_group_(comm, group, tag, newcomm, ierr); }
void MPI_COMM_SPLIT(int *comm, int *color, int *key, int *newcomm, int *ierr){ mpi_comm_split_(comm, color, key, newcomm, ierr); }
void MPI_COMM_SPLIT_TYPE(int *comm, int *split_type, int *key, int *info, int *newcomm, int *ierr){ mpi_comm_split_type_(comm, split_type, key, info, newcomm, ierr); }
void MPI_COMM_FREE(int *comm, int *ierr){ mpi_comm_free_(comm, ierr); }

#ifdef __cplusplus
}
#endif

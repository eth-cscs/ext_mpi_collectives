#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include "constants.h"
#include "hash_table.h"
#include "hash_table_blocking.h"
#include "ext_mpi.h"
#include "ext_mpi_blocking.h"
#include "ext_mpi_interface.h"

int ext_mpi_is_blocking = 0;
static int comms_blocking[100];

int MPI_Init(int *argc, char ***argv){
  int mpi_comm_rank, var, var2, i;
  char *c;
  MPI_Comm comm = MPI_COMM_WORLD;
  int ret = PMPI_Init(argc, argv);
  ext_mpi_hash_init();
  EXT_MPI_Init();
  PMPI_Comm_rank(MPI_COMM_WORLD, &mpi_comm_rank);
  if (mpi_comm_rank == 0) {
    var = ((c = getenv("EXT_MPI_BLOCKING")) != NULL);
    var2 = ((c = getenv("EXT_MPI_VERBOSE")) != NULL);
    if (var) {
      ext_mpi_is_blocking = 1;
    }
    if (var && var2) {
      printf("# EXT_MPI blocking\n");
    }
  }
  MPI_Bcast(&ext_mpi_is_blocking, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (ext_mpi_is_blocking) {
    ext_mpi_hash_init_blocking();
    ext_mpi_hash_insert_blocking(&comm, 0);
    EXT_MPI_Init_blocking_comm(MPI_COMM_WORLD, 0);
    for (i = 0; i < sizeof(comms_blocking)/sizeof(int); i++) {
      comms_blocking[i] = 0;
    }
    comms_blocking[0] = 1;
  }
  return ret;
}

int MPI_Finalize(){
  if (ext_mpi_is_blocking) {
    EXT_MPI_Finalize_blocking_comm(0);
    ext_mpi_hash_done_blocking();
  }
  EXT_MPI_Finalize();
  ext_mpi_hash_done();
  ext_mpi_is_blocking = 0;
  return PMPI_Finalize();
}

int MPI_Allreduce_init(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int handle, ret;
  MPI_Recv_init(NULL, 0, datatype, 0, 0, comm, request);
  if (!(ret=EXT_MPI_Allreduce_init(sendbuf, recvbuf, count, datatype, op, comm, info, &handle))){
    ext_mpi_hash_insert(request, handle);
  }
  return ret;
}

int MPI_Allgatherv_init(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, const int *recvcounts, const int *displs, MPI_Datatype recvtype, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int handle, ret;
  MPI_Recv_init(NULL, 0, recvtype, 0, 0, comm, request);
  if (!(ret=EXT_MPI_Allgatherv_init(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm, info, &handle))){
    ext_mpi_hash_insert(request, handle);
  }
  return ret;
}

int MPI_Reduce_scatter_init(const void *sendbuf, void *recvbuf, const int recvcounts[], MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int handle, ret;
  MPI_Recv_init(NULL, 0, datatype, 0, 0, comm, request);
  if (!(ret=EXT_MPI_Reduce_scatter_init(sendbuf, recvbuf, recvcounts, datatype, op, comm, info, &handle))){
    ext_mpi_hash_insert(request, handle);
  }
  return ret;
}

int MPI_Bcast_init(void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int handle, ret;
  MPI_Recv_init(NULL, 0, datatype, 0, 0, comm, request);
  if (!(ret=EXT_MPI_Bcast_init(buffer, count, datatype, root, comm, info, &handle))){
    ext_mpi_hash_insert(request, handle);
  }
  return ret;
}

int MPI_Reduce_init(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int handle, ret;
  MPI_Recv_init(NULL, 0, datatype, 0, 0, comm, request);
  if (!(ret=EXT_MPI_Reduce_init(sendbuf, recvbuf, count, datatype, op, root, comm, info, &handle))){
    ext_mpi_hash_insert(request, handle);
  }
  return ret;
}

int MPI_Gatherv_init(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, const int *recvcounts, const int *displs, MPI_Datatype recvtype, int root, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int handle, ret;
  MPI_Recv_init(NULL, 0, recvtype, 0, 0, comm, request);
  if (!(ret=EXT_MPI_Gatherv_init(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, root, comm, info, &handle))){
    ext_mpi_hash_insert(request, handle);
  }
  return ret;
}

int MPI_Scatterv_init(const void *sendbuf, const int *sendcounts, const int *displs, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int handle, ret;
  MPI_Recv_init(NULL, 0, sendtype, 0, 0, comm, request);
  if (!(ret=EXT_MPI_Scatterv_init(sendbuf, sendcounts, displs, sendtype, recvbuf, recvcount, recvtype, root, comm, info, &handle))){
    ext_mpi_hash_insert(request, handle);
  }
  return ret;
}

int MPI_Request_free(MPI_Request *request){
  int handle=ext_mpi_hash_search(request);
  if (handle >= 0){
    EXT_MPI_Done(handle);
    ext_mpi_hash_delete(request);
  }
  return PMPI_Request_free(request);
}

int MPI_Start(MPI_Request *request){
  int handle=ext_mpi_hash_search(request);
  if (handle >= 0){
    EXT_MPI_Start(handle);
    return MPI_SUCCESS;
  }else{
    return PMPI_Start(request);
  }
}

int MPI_Wait(MPI_Request *request, MPI_Status *status){
  int handle=ext_mpi_hash_search(request);
  if (handle >= 0){
    EXT_MPI_Wait(handle);
    return MPI_SUCCESS;
  }else{
    return PMPI_Wait(request, status);
  }
}

static int is_in_request_array(int count, MPI_Request array_of_requests[]){
  int i;
  for (i=0; i<count; i++){
    if (ext_mpi_hash_search(&array_of_requests[i])>=0){
      return 1;
    }
  }
  return 0;
}

int MPI_Waitall(int count, MPI_Request array_of_requests[], MPI_Status array_of_statuses[]){
  int ret, i;
  if (!is_in_request_array(count, array_of_requests)){
    return PMPI_Waitall(count, array_of_requests, array_of_statuses);
  }
  if (array_of_statuses == MPI_STATUSES_IGNORE){
    for (i=0; i<count; i++){
      ret = MPI_Wait(&array_of_requests[i], MPI_STATUS_IGNORE);
      if (ret != MPI_SUCCESS){
        return ret;
      }
    }
  }else{
    for (i=0; i<count; i++){
      ret = MPI_Wait(&array_of_requests[i], &array_of_statuses[i]);
      if (ret != MPI_SUCCESS){
        return ret;
      }
    }
  }
  return MPI_SUCCESS;
}

int MPI_Waitany(int count, MPI_Request array_of_requests[], int *indx, MPI_Status *status){
  int ret, flag, i;
  if (!is_in_request_array(count, array_of_requests)){
    return PMPI_Waitany(count, array_of_requests, indx, status);
  }
  while (1){
    for (i=0; i<count; i++){
      ret = MPI_Test(&array_of_requests[i], &flag, status);
      if (ret != MPI_SUCCESS){
        return ret;
      }
      if (flag){
        *indx = i;
        return MPI_SUCCESS;
      }
    }
  }
  return MPI_SUCCESS;
}

int MPI_Waitsome(int incount, MPI_Request array_of_requests[], int *outcount, int array_of_indices[], MPI_Status array_of_statuses[]){
  if (!is_in_request_array(incount, array_of_requests)){
    return PMPI_Waitsome(incount, array_of_requests, outcount, array_of_indices, array_of_statuses);
  }
  *outcount = 1;
  if (array_of_statuses == MPI_STATUSES_IGNORE){
    return MPI_Waitany(incount, array_of_requests, &array_of_indices[0], MPI_STATUS_IGNORE);
  }else{
    return MPI_Waitany(incount, array_of_requests, &array_of_indices[0], &array_of_statuses[0]);
  }
}

int MPI_Test(MPI_Request *request, int *flag, MPI_Status *status){
  int handle=ext_mpi_hash_search(request);
  if (handle >= 0){
    *flag=EXT_MPI_Test(handle);
    return MPI_SUCCESS;
  }else{
    return PMPI_Test(request, flag, status);
  }
}

int MPI_Testall(int count, MPI_Request array_of_requests[], int *flag, MPI_Status array_of_statuses[]){
  int ret, lflag, i;
  if (!is_in_request_array(count, array_of_requests)){
    return PMPI_Testall(count, array_of_requests, flag, array_of_statuses);
  }
  if (array_of_statuses == MPI_STATUSES_IGNORE){
    *flag = 1;
    for (i=0; i<count; i++){
      ret = MPI_Test(&array_of_requests[i], &lflag, MPI_STATUS_IGNORE);
      if (ret != MPI_SUCCESS){
        return ret;
      }
      if (!lflag) flag=0;
    }
  }else{
    *flag = 1;
    for (i=0; i<count; i++){
      ret = MPI_Test(&array_of_requests[i], &lflag, &array_of_statuses[i]);
      if (ret != MPI_SUCCESS){
        return ret;
      }
      if (!lflag) *flag=0;
    }
  }
  return MPI_SUCCESS;
}

int MPI_Testany(int count, MPI_Request array_of_requests[], int *indx, int *flag, MPI_Status *status){
  int ret, lflag, i;
  if (!is_in_request_array(count, array_of_requests)){
    return PMPI_Testany(count, array_of_requests, indx, flag, status);
  }
  *flag = 0;
  for (i=0; i<count; i++){
    ret = MPI_Test(&array_of_requests[i], &lflag, status);
    if (ret != MPI_SUCCESS){
      return ret;
    }
    if (lflag){
      *flag = 1;
      *indx = i;
      return MPI_SUCCESS;
    }
  }
  return MPI_SUCCESS;
}

int MPI_Testsome(int incount, MPI_Request array_of_requests[], int *outcount, int array_of_indices[], MPI_Status array_of_statuses[]){
  int flag;
  if (!is_in_request_array(incount, array_of_requests)){
    return PMPI_Testsome(incount, array_of_requests, outcount, array_of_indices, array_of_statuses);
  }
  *outcount = 1;
  if (array_of_statuses == MPI_STATUSES_IGNORE){
    return MPI_Testany(incount, array_of_requests, &array_of_indices[0], &flag, MPI_STATUS_IGNORE);
  }else{
    return MPI_Testany(incount, array_of_requests, &array_of_indices[0], &flag, &array_of_statuses[0]);
  }
}

int MPI_Allgather_init(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int mpi_size, *recvcounts=NULL, *displs=NULL, ret, i;
  MPI_Comm_size(comm, &mpi_size);
  recvcounts=(int*)malloc(mpi_size*sizeof(int));
  if (recvcounts==NULL) goto error;
  displs=(int*)malloc(mpi_size*sizeof(int));
  if (displs==NULL) goto error;
  for (i=0; i<mpi_size; i++){
    recvcounts[i]=recvcount;
    displs[i]=i*recvcount;
  }
  ret=MPI_Allgatherv_init(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm, info, request);
  free(displs);
  free(recvcounts);
  return ret;
error:
  free(displs);
  free(recvcounts);
  return -1;
}

int MPI_Reduce_scatter_block_init(const void *sendbuf, void *recvbuf, int recvcount, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int mpi_size, *recvcounts=NULL, ret, i;
  MPI_Comm_size(comm, &mpi_size);
  recvcounts=(int*)malloc(mpi_size*sizeof(int));
  if (recvcounts==NULL) goto error;
  for (i=0; i<mpi_size; i++){
    recvcounts[i]=recvcount;
  }
  ret=MPI_Reduce_scatter_init(sendbuf, recvbuf, recvcounts, datatype, op, comm, info, request);
  free(recvcounts);
  return ret;
error:
  free(recvcounts);
  return -1;
}

int MPI_Gather_init(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int mpi_size, *recvcounts=NULL, *displs=NULL, ret, i;
  MPI_Comm_size(comm, &mpi_size);
  recvcounts=(int*)malloc(mpi_size*sizeof(int));
  if (recvcounts==NULL) goto error;
  displs=(int*)malloc(mpi_size*sizeof(int));
  if (displs==NULL) goto error;
  for (i=0; i<mpi_size; i++){
    recvcounts[i]=recvcount;
    displs[i]=i*recvcount;
  }
  ret=MPI_Gatherv_init(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, root, comm, info, request);
  free(displs);
  free(recvcounts);
  return ret;
error:
  free(displs);
  free(recvcounts);
  return -1;
}

int MPI_Scatter_init(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int mpi_size, *sendcounts=NULL, *displs=NULL, ret, i;
  MPI_Comm_size(comm, &mpi_size);
  sendcounts=(int*)malloc(mpi_size*sizeof(int));
  if (sendcounts==NULL) goto error;
  displs=(int*)malloc(mpi_size*sizeof(int));
  if (displs==NULL) goto error;
  for (i=0; i<mpi_size; i++){
    sendcounts[i]=sendcount;
    displs[i]=i*sendcount;
  }
  ret=MPI_Scatterv_init(sendbuf, sendcounts, displs, sendtype, recvbuf, recvcount, recvtype, root, comm, info, request);
  free(displs);
  free(sendcounts);
  return ret;
error:
  free(displs);
  free(sendcounts);
  return -1;
}

int MPI_Allreduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm){
  int reduction_op, i;
  if (ext_mpi_is_blocking && op == MPI_SUM) {
    if (datatype == MPI_DOUBLE) {
      reduction_op = OPCODE_REDUCE_SUM_DOUBLE;
    } else if (datatype == MPI_LONG) {
      reduction_op = OPCODE_REDUCE_SUM_LONG_INT;
    } else if (datatype == MPI_FLOAT) {
      reduction_op = OPCODE_REDUCE_SUM_FLOAT;
    } else if (datatype == MPI_INT) {
      reduction_op = OPCODE_REDUCE_SUM_INT;
    } else {
      return PMPI_Allreduce(sendbuf, recvbuf, count, datatype, op, comm);
    }
    i = ext_mpi_hash_search_blocking(&comm);
    if (i >= 0) {
      return EXT_MPI_Allreduce(sendbuf, recvbuf, count, reduction_op, i);
    } else {
      return PMPI_Allreduce(sendbuf, recvbuf, count, datatype, op, comm);
    }
  } else {
    return PMPI_Allreduce(sendbuf, recvbuf, count, datatype, op, comm);
  }
}

int MPI_Reduce_scatter_block(const void *sendbuf, void *recvbuf, int recvcount, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm){
  int reduction_op, lcount, i;
  if (ext_mpi_is_blocking && op == MPI_SUM) {
    if (datatype == MPI_DOUBLE) {
      reduction_op = OPCODE_REDUCE_SUM_DOUBLE;
      lcount = recvcount * sizeof(double);
    } else if (datatype == MPI_LONG) {
      reduction_op = OPCODE_REDUCE_SUM_LONG_INT;
      lcount = recvcount * sizeof(long int);
    } else if (datatype == MPI_FLOAT) {
      reduction_op = OPCODE_REDUCE_SUM_FLOAT;
      lcount = recvcount * sizeof(float);
    } else if (datatype == MPI_INT) {
      reduction_op = OPCODE_REDUCE_SUM_INT;
      lcount = recvcount * sizeof(int);
    } else {
      return PMPI_Reduce_scatter_block(sendbuf, recvbuf, recvcount, datatype, op, comm);
    }
    i = ext_mpi_hash_search_blocking(&comm);
    if (i >= 0) {
      return EXT_MPI_Reduce_scatter_block(sendbuf, recvbuf, lcount, reduction_op, i);
    } else {
      return PMPI_Reduce_scatter_block(sendbuf, recvbuf, recvcount, datatype, op, comm);
    }
  } else {
    return PMPI_Reduce_scatter_block(sendbuf, recvbuf, recvcount, datatype, op, comm);
  }
}

int MPI_Allgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm){
  int type_size, i;
  if (ext_mpi_is_blocking) {
    i = ext_mpi_hash_search_blocking(&comm);
    if (i >= 0) {
      MPI_Type_size(sendtype, &type_size);
      return EXT_MPI_Allgather(sendbuf, type_size * sendcount, sendtype, recvbuf, type_size * recvcount, recvtype, comm, i);
    } else {
      return PMPI_Allgather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm);
    }
  } else {
    return PMPI_Allgather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm);
  }
}

static int add_comm_to_blocking(MPI_Comm *comm){
  int i = 0;
  while (comms_blocking[i]) i++;
  comms_blocking[i] = 1;
  ext_mpi_hash_insert_blocking(comm, i);
  EXT_MPI_Init_blocking_comm(*comm, i);
  return 0;
}

static int remove_comm_from_blocking(MPI_Comm *comm){
  int i = ext_mpi_hash_search_blocking(comm);
  EXT_MPI_Finalize_blocking_comm(i);
  ext_mpi_hash_delete_blocking(comm);
  comms_blocking[i] = 0;
  return 0;
}

int MPI_Comm_dup(MPI_Comm comm, MPI_Comm *newcomm) {
  int ret;
  ret = PMPI_Comm_dup(comm, newcomm);
  if (ext_mpi_is_blocking) {
    add_comm_to_blocking(newcomm);
  }
  return ret;
}

int MPI_Comm_create(MPI_Comm comm, MPI_Group group, MPI_Comm *newcomm) {
  int ret;
  ret = PMPI_Comm_create(comm, group, newcomm);
  if (ext_mpi_is_blocking) {
    add_comm_to_blocking(newcomm);
  }
  return ret;
}

int MPI_Comm_split(MPI_Comm comm, int color, int key, MPI_Comm *newcomm) {
  int ret;
  ret = PMPI_Comm_split(comm, color, key, newcomm);
  if (ext_mpi_is_blocking) {
    add_comm_to_blocking(newcomm);
  }
  return ret;
}

int MPI_Comm_split_type(MPI_Comm comm, int split_type, int key, MPI_Info info, MPI_Comm *newcomm) {
  int ret;
  ret = PMPI_Comm_split_type(comm, split_type, key, info, newcomm);
  if (ext_mpi_is_blocking) {
    add_comm_to_blocking(newcomm);
  }
  return ret;
}

int MPI_Comm_free(MPI_Comm *comm) {
  if (ext_mpi_is_blocking) {
    remove_comm_from_blocking(comm);
  }
  return PMPI_Comm_free(comm);
}

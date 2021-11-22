#include <stdlib.h>
#include <mpi.h>
#include "hash_table.h"
#include "ext_mpi.h"
#include "ext_mpi_interface.h"

int MPI_Init(int *argc, char ***argv){
  int ret = PMPI_Init(argc, argv);
  EXT_MPI_Init();
  ext_mpi_hash_init();
  return ret;
}

int MPI_Finalize(){
  EXT_MPI_Finalize();
  return PMPI_Finalize();
}

int MPI_Allreduce_init(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int handle, ret;
  MPI_Recv_init(NULL, 0, datatype, 0, 0, comm, request);
  if (!(ret=EXT_MPI_Allreduce_init(sendbuf, recvbuf, count, datatype, op, comm, &handle))){
    ext_mpi_hash_insert(request, handle);
  }
  return ret;
}

int MPI_Allgatherv_init(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, const int *recvcounts, const int *displs, MPI_Datatype recvtype, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int handle, ret;
  MPI_Recv_init(NULL, 0, recvtype, 0, 0, comm, request);
  if (!(ret=EXT_MPI_Allgatherv_init(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm, &handle))){
    ext_mpi_hash_insert(request, handle);
  }
  return ret;
}

int MPI_Reduce_scatter_init(const void *sendbuf, void *recvbuf, const int recvcounts[], MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int handle, ret;
  MPI_Recv_init(NULL, 0, datatype, 0, 0, comm, request);
  if (!(ret=EXT_MPI_Reduce_scatter_init(sendbuf, recvbuf, recvcounts, datatype, op, comm, &handle))){
    ext_mpi_hash_insert(request, handle);
  }
  return ret;
}

int MPI_Bcast_init(void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int handle, ret;
  MPI_Recv_init(NULL, 0, datatype, 0, 0, comm, request);
  if (!(ret=EXT_MPI_Bcast_init(buffer, count, datatype, root, comm, &handle))){
    ext_mpi_hash_insert(request, handle);
  }
  return ret;
}

int MPI_Reduce_init(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int handle, ret;
  MPI_Recv_init(NULL, 0, datatype, 0, 0, comm, request);
  if (!(ret=EXT_MPI_Reduce_init(sendbuf, recvbuf, count, datatype, op, root, comm, &handle))){
    ext_mpi_hash_insert(request, handle);
  }
  return ret;
}

int MPI_Gatherv_init(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, const int *recvcounts, const int *displs, MPI_Datatype recvtype, int root, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int handle, ret;
  MPI_Recv_init(NULL, 0, recvtype, 0, 0, comm, request);
  if (!(ret=EXT_MPI_Gatherv_init(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, root, comm, &handle))){
    ext_mpi_hash_insert(request, handle);
  }
  return ret;
}

int MPI_Scatterv_init(const void *sendbuf, const int *sendcounts, const int *displs, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm, MPI_Info info, MPI_Request *request){
  int handle, ret;
  MPI_Recv_init(NULL, 0, sendtype, 0, 0, comm, request);
  if (!(ret=EXT_MPI_Scatterv_init(sendbuf, sendcounts, displs, sendtype, recvbuf, recvcount, recvtype, root, comm, &handle))){
    ext_mpi_hash_insert(request, handle);
  }
  return ret;
}

int MPI_Request_free(MPI_Request *request){
  int handle=ext_mpi_hash_search(request);
  if (handle >= 0){
    EXT_MPI_Done(handle);
  }
  return PMPI_Request_free(request);
}

int MPI_Start(MPI_Request *request){
  int handle=ext_mpi_hash_search(request);
  if (handle >= 0){
    EXT_MPI_Start(handle);
    return 0;
  }else{
    return PMPI_Start(request);
  }
}

int MPI_Wait(MPI_Request *request, MPI_Status *status){
  int handle=ext_mpi_hash_search(request);
  if (handle >= 0){
    EXT_MPI_Wait(handle);
    return 0;
  }else{
    return PMPI_Wait(request, status);
  }
}

int MPI_Test(MPI_Request *request, int *flag, MPI_Status *status){
  int handle=ext_mpi_hash_search(request);
  if (handle >= 0){
    *flag=EXT_MPI_Test(handle);
    return 0;
  }else{
    return PMPI_Test(request, flag, status);
  }
}

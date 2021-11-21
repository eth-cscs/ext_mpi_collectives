#include <mpi.h>
#include "hash_table.h"
#include "ext_mpi.h"

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

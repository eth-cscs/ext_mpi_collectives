#ifndef EXT_MPI_DEBUG_PERSISTENT_H_

#define EXT_MPI_DEBUG_PERSISTENT_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

int ext_mpi_allgatherv_init_debug(const void *sendbuf, int sendcount,
                                  MPI_Datatype sendtype, void *recvbuf,
                                  const int *recvcounts, const int *displs,
                                  MPI_Datatype recvtype, MPI_Comm comm,
                                  MPI_Info info, int *handle);
int ext_mpi_gatherv_init_debug(const void *sendbuf, int sendcount,
                               MPI_Datatype sendtype, void *recvbuf,
                               const int *recvcounts, const int *displs,
                               MPI_Datatype recvtype, int root,
                               MPI_Comm comm,
                               MPI_Info info, int *handle);
int ext_mpi_reduce_scatter_init_debug(
    const void *sendbuf, void *recvbuf, const int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm, MPI_Info info, int *handle);
int ext_mpi_scatterv_init_debug(const void *sendbuf, const int *sendcounts, const int *displs,
                                MPI_Datatype sendtype, void *recvbuf,
                                int recvcount, MPI_Datatype recvtype,
                                int root, MPI_Comm comm,
                                MPI_Info info, int *handle);
int ext_mpi_allreduce_init_debug(const void *sendbuf, void *recvbuf, int count,
                                 MPI_Datatype datatype, MPI_Op op,
                                 MPI_Comm comm,
                                 MPI_Info info, int *handle);
int ext_mpi_reduce_init_debug(const void *sendbuf, void *recvbuf, int count,
                              MPI_Datatype datatype, MPI_Op op, int root,
                              MPI_Comm comm,
                              MPI_Info info, int *handle);
int ext_mpi_bcast_init_debug(void *buffer, int count, MPI_Datatype datatype,
                             int root, MPI_Comm comm,
                             MPI_Info info, int *handle);

#ifdef __cplusplus
}
#endif

#endif

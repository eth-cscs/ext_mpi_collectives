#include "ext_mpi_alltoall.h"

#ifdef __cplusplus
extern "C" {
#endif

void ext_mpi_alltoall_init_general_(
    void *sendbuf, int *sendcount, MPI_Datatype *sendtype, void *recvbuf,
    int *recvcount, MPI_Datatype *recvtype, MPI_Comm *comm_row,
    int *my_cores_per_node_row, MPI_Comm *comm_column,
    int *my_cores_per_node_column, int *handle, int *ierr) {
  *ierr = EXT_MPI_Alltoall_init_general(
      sendbuf, *sendcount, *sendtype, recvbuf, *recvcount, *recvtype, *comm_row,
      *my_cores_per_node_row, *comm_column, *my_cores_per_node_column, handle);
}

void ext_mpi_alltoallv_init_general_(
    void *sendbuf, int *sendcounts, int *sdispls, MPI_Datatype *sendtype,
    void *recvbuf, int *recvcounts, int *rdispls, MPI_Datatype *recvtype,
    MPI_Comm *comm_row, int *my_cores_per_node_row, MPI_Comm *comm_column,
    int *my_cores_per_node_column, int *handle, int *ierr) {
  *ierr = EXT_MPI_Alltoallv_init_general(
      sendbuf, sendcounts, sdispls, *sendtype, recvbuf, recvcounts, rdispls,
      *recvtype, *comm_row, *my_cores_per_node_row, *comm_column,
      *my_cores_per_node_column, handle);
}

void ext_mpi_ialltoall_init_general_(
    void *sendbuf, int *sendcount, MPI_Datatype *sendtype, void *recvbuf,
    int *recvcount, MPI_Datatype *recvtype, MPI_Comm *comm_row,
    int *my_cores_per_node_row, MPI_Comm *comm_column,
    int *my_cores_per_node_column, int *handle_begin, int *handle_wait,
    int *ierr) {
  *ierr = EXT_MPI_Ialltoall_init_general(
      sendbuf, *sendcount, *sendtype, recvbuf, *recvcount, *recvtype, *comm_row,
      *my_cores_per_node_row, *comm_column, *my_cores_per_node_column,
      handle_begin, handle_wait);
}

void ext_mpi_ialltoallv_init_general_(
    void *sendbuf, int *sendcounts, int *sdispls, MPI_Datatype *sendtype,
    void *recvbuf, int *recvcounts, int *rdispls, MPI_Datatype *recvtype,
    MPI_Comm *comm_row, int *my_cores_per_node_row, MPI_Comm *comm_column,
    int *my_cores_per_node_column, int *handle_begin, int *handle_wait,
    int *ierr) {
  *ierr = EXT_MPI_Ialltoallv_init_general(
      sendbuf, sendcounts, sdispls, *sendtype, recvbuf, recvcounts, rdispls,
      *recvtype, *comm_row, *my_cores_per_node_row, *comm_column,
      *my_cores_per_node_column, handle_begin, handle_wait);
}

void ext_mpi_alltoall_init_(void *sendbuf, int *sendcount,
                            MPI_Datatype *sendtype, void *recvbuf,
                            int *recvcount, MPI_Datatype *recvtype,
                            MPI_Comm *comm, int *handle, int *ierr) {
  *ierr = EXT_MPI_Alltoall_init(sendbuf, *sendcount, *sendtype, recvbuf,
                                *recvcount, *recvtype, *comm, handle);
}

void ext_mpi_alltoallv_init_(void *sendbuf, int *sendcounts, int *sdispls,
                             MPI_Datatype *sendtype, void *recvbuf,
                             int *recvcounts, int *rdispls,
                             MPI_Datatype *recvtype, MPI_Comm *comm,
                             int *handle, int *ierr) {
  *ierr =
      EXT_MPI_Alltoallv_init(sendbuf, sendcounts, sdispls, *sendtype, recvbuf,
                             recvcounts, rdispls, *recvtype, *comm, handle);
}

void ext_mpi_ialltoall_init_(void *sendbuf, int *sendcount,
                             MPI_Datatype *sendtype, void *recvbuf,
                             int *recvcount, MPI_Datatype *recvtype,
                             MPI_Comm *comm, int *handle_begin,
                             int *handle_wait, int *ierr) {
  *ierr = EXT_MPI_Ialltoall_init(sendbuf, *sendcount, *sendtype, recvbuf,
                                 *recvcount, *recvtype, *comm, handle_begin,
                                 handle_wait);
}

void ext_mpi_ialltoallv_init_(void *sendbuf, int *sendcounts, int *sdispls,
                              MPI_Datatype *sendtype, void *recvbuf,
                              int *recvcounts, int *rdispls,
                              MPI_Datatype *recvtype, MPI_Comm *comm,
                              int *handle_begin, int *handle_wait, int *ierr) {
  *ierr = EXT_MPI_Ialltoallv_init(sendbuf, sendcounts, sdispls, *sendtype,
                                  recvbuf, recvcounts, rdispls, *recvtype,
                                  *comm, handle_begin, handle_wait);
}

void ext_mpi_allgatherv_init_general_(
    void *sendbuf, int *sendcount, MPI_Datatype *sendtype, void *recvbuf,
    int *recvcounts, int *displs, MPI_Datatype *recvtype, MPI_Comm *comm_row,
    int *my_cores_per_node_row, MPI_Comm *comm_column,
    int *my_cores_per_node_column, int *handle, int *ierr) {
  *ierr = EXT_MPI_Allgatherv_init_general(
      sendbuf, *sendcount, *sendtype, recvbuf, recvcounts, displs, *recvtype,
      *comm_row, *my_cores_per_node_row, *comm_column,
      *my_cores_per_node_column, handle);
}

void ext_mpi_reduce_scatter_init_general_(
    void *sendbuf, void *recvbuf, int *recvcounts, MPI_Datatype *datatype,
    MPI_Op *op, MPI_Comm *comm_row, int *my_cores_per_node_row,
    MPI_Comm *comm_column, int *my_cores_per_node_column, int *handle,
    int *ierr) {
  *ierr = EXT_MPI_Reduce_scatter_init_general(
      sendbuf, recvbuf, recvcounts, *datatype, *op, *comm_row,
      *my_cores_per_node_row, *comm_column, *my_cores_per_node_column, handle);
}

void ext_mpi_allreduce_init_general_(void *sendbuf, void *recvbuf, int *count,
                                     MPI_Datatype *datatype, MPI_Op *op,
                                     MPI_Comm *comm_row,
                                     int *my_cores_per_node_row,
                                     MPI_Comm *comm_column,
                                     int *my_cores_per_node_column, int *handle,
                                     int *ierr) {
  *ierr = EXT_MPI_Allreduce_init_general(
      sendbuf, recvbuf, *count, *datatype, *op, *comm_row,
      *my_cores_per_node_row, *comm_column, *my_cores_per_node_column, handle);
}

void ext_mpi_allgatherv_init_(void *sendbuf, int *sendcount,
                              MPI_Datatype *sendtype, void *recvbuf,
                              int *recvcounts, int *displs,
                              MPI_Datatype *recvtype, MPI_Comm *comm,
                              int *handle, int *ierr) {
  *ierr = EXT_MPI_Allgatherv_init(sendbuf, *sendcount, *sendtype, recvbuf,
                                  recvcounts, displs, *recvtype, *comm, handle);
}

void ext_mpi_reduce_scatter_init_(void *sendbuf, void *recvbuf, int *recvcounts,
                                  MPI_Datatype *datatype, MPI_Op *op,
                                  MPI_Comm *comm, int *handle, int *ierr) {
  *ierr = EXT_MPI_Reduce_scatter_init(sendbuf, recvbuf, recvcounts, *datatype,
                                      *op, *comm, handle);
}

void ext_mpi_allreduce_init_(void *sendbuf, void *recvbuf, int *count,
                             MPI_Datatype *datatype, MPI_Op *op, MPI_Comm *comm,
                             int *handle, int *ierr) {
  *ierr = EXT_MPI_Allreduce_init(sendbuf, recvbuf, *count, *datatype, *op,
                                 *comm, handle);
}

void ext_mpi_exec_(int *handle, int *ierr) { *ierr = EXT_MPI_Exec(*handle); }

void ext_mpi_done_(int *handle, int *ierr) { *ierr = EXT_MPI_Done(*handle); }

#ifdef __cplusplus
}
#endif

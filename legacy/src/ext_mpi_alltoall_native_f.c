#include "ext_mpi_alltoall_native.h"

#ifdef __cplusplus
extern "C" {
#endif

void ext_mpi_alltoall_init_native_(
    void *sendbuf, int *sendcount, MPI_Datatype *sendtype, void *recvbuf,
    int *recvcount, MPI_Datatype *recvtype, MPI_Comm *comm_row,
    int *ext_mpi_cores_per_node_row, MPI_Comm *comm_column,
    int *ext_mpi_cores_per_node_column, int *handle, int *num_ports,
    int *num_active_ports, int *chunks_throttle) {
  *handle = EXT_MPI_Alltoall_init_native(
      sendbuf, *sendcount, *sendtype, recvbuf, *recvcount, *recvtype, *comm_row,
      *ext_mpi_cores_per_node_row, *comm_column, *ext_mpi_cores_per_node_column,
      *num_ports, *num_active_ports, *chunks_throttle);
}

void ext_mpi_alltoallv_init_native_(
    void *sendbuf, int *sendcounts, int *sdispls, MPI_Datatype *sendtype,
    void *recvbuf, int *recvcounts, int *rdispls, MPI_Datatype *recvtype,
    MPI_Comm *comm_row, int *ext_mpi_cores_per_node_row, MPI_Comm *comm_column,
    int *ext_mpi_cores_per_node_column, int *num_active_ports, int *handle,
    int *chunks_throttle) {
  *handle = EXT_MPI_Alltoallv_init_native(
      sendbuf, sendcounts, sdispls, *sendtype, recvbuf, recvcounts, rdispls,
      *recvtype, *comm_row, *ext_mpi_cores_per_node_row, *comm_column,
      *ext_mpi_cores_per_node_column, *num_active_ports, *chunks_throttle);
}

void ext_mpi_ialltoall_init_native_(
    void *sendbuf, int *sendcount, MPI_Datatype *sendtype, void *recvbuf,
    int *recvcount, MPI_Datatype *recvtype, MPI_Comm *comm_row,
    int *ext_mpi_cores_per_node_row, MPI_Comm *comm_column,
    int *ext_mpi_cores_per_node_column, int *num_active_ports,
    int *handle_begin, int *handle_wait) {
  EXT_MPI_Ialltoall_init_native(
      sendbuf, *sendcount, *sendtype, recvbuf, *recvcount, *recvtype, *comm_row,
      *ext_mpi_cores_per_node_row, *comm_column, *ext_mpi_cores_per_node_column,
      *num_active_ports, handle_begin, handle_wait);
}

void ext_mpi_ialltoallv_init_native_(
    void *sendbuf, int *sendcounts, int *sdispls, MPI_Datatype *sendtype,
    void *recvbuf, int *recvcounts, int *rdispls, MPI_Datatype *recvtype,
    MPI_Comm *comm_row, int *ext_mpi_cores_per_node_row, MPI_Comm *comm_column,
    int *ext_mpi_cores_per_node_column, int *num_active_ports,
    int *handle_begin, int *handle_wait) {
  EXT_MPI_Ialltoallv_init_native(sendbuf, sendcounts, sdispls, *sendtype,
                                 recvbuf, recvcounts, rdispls, *recvtype,
                                 *comm_row, *ext_mpi_cores_per_node_row,
                                 *comm_column, *ext_mpi_cores_per_node_column,
                                 *num_active_ports, handle_begin, handle_wait);
}

void ext_mpi_allgatherv_init_native_(
    void *sendbuf, int *sendcount, MPI_Datatype *sendtype, void *recvbuf,
    int *recvcounts, int *displs, MPI_Datatype *recvtype, MPI_Comm *comm_row,
    int *my_cores_per_node_row, MPI_Comm *comm_column,
    int *my_cores_per_node_column, int *num_ports, int *num_parallel,
    int *num_active_ports, int allreduce, int bruck, int recvcount,
    int *handle) {
  *handle = EXT_MPI_Allgatherv_init_native(
      sendbuf, *sendcount, *sendtype, recvbuf, recvcounts, displs, *recvtype,
      *comm_row, *my_cores_per_node_row, *comm_column,
      *my_cores_per_node_column, num_ports, num_parallel, *num_active_ports,
      allreduce, bruck, recvcount);
}

void ext_mpi_reduce_scatter_init_native_(
    void *sendbuf, void *recvbuf, int *recvcounts, MPI_Datatype *datatype,
    MPI_Op *op, MPI_Comm *comm_row, int *my_cores_per_node_row,
    MPI_Comm *comm_column, int *my_cores_per_node_column, int *num_ports,
    int *num_parallel, int *num_active_ports, int copyin, int allreduce,
    int bruck, int recvcount, int *handle) {
  *handle = EXT_MPI_Reduce_scatter_init_native(
      sendbuf, recvbuf, recvcounts, *datatype, *op, *comm_row,
      *my_cores_per_node_row, *comm_column, *my_cores_per_node_column,
      num_ports, num_parallel, *num_active_ports, copyin, allreduce, bruck,
      recvcount);
}

void ext_mpi_allreduce_init_native_(
    void *sendbuf, void *recvbuf, int *counts, MPI_Datatype *datatype,
    MPI_Op *op, MPI_Comm *comm_row, int *my_cores_per_node_row,
    MPI_Comm *comm_column, int *my_cores_per_node_column, int *num_ports,
    int *num_ports_limit, int *num_active_ports, int copyin, int *handle) {
  *handle = EXT_MPI_Allreduce_init_native(
      sendbuf, recvbuf, *counts, *datatype, *op, *comm_row,
      *my_cores_per_node_row, *comm_column, *my_cores_per_node_column,
      num_ports, num_ports_limit, *num_active_ports, copyin);
}

void ext_mpi_merge_collectives_native_(int *handle1, int *handle2,
                                       int *handle) {
  *handle = EXT_MPI_Merge_collectives_native(*handle1, *handle2);
}

void ext_mpi_exec_native_(int *handle, int *err) {
  *err = EXT_MPI_Exec_native(*handle);
}

void ext_mpi_done_native_(int *handle, int *err) {
  *err = EXT_MPI_Done_native(*handle);
}

#ifdef __cplusplus
}
#endif

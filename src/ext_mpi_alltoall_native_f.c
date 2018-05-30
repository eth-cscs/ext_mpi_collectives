#include "ext_mpi_alltoall_native.h"

void
ext_mpi_alltoall_init_native_ (void *sendbuf, int *sendcount,
			       MPI_Datatype * sendtype, void *recvbuf,
			       int *recvcount, MPI_Datatype * recvtype,
			       MPI_Comm * comm_row,
			       int *ext_mpi_cores_per_node_row,
			       MPI_Comm * comm_column,
			       int *ext_mpi_cores_per_node_column,
			       int *handle, int *num_ports,
			       int *num_active_ports, int *chunks_throttle)
{
  *handle =
    EXT_MPI_Alltoall_init_native (sendbuf, *sendcount, *sendtype, recvbuf,
				  *recvcount, *recvtype, *comm_row,
				  *ext_mpi_cores_per_node_row, *comm_column,
				  *ext_mpi_cores_per_node_column, *num_ports,
				  *num_active_ports, *chunks_throttle);
}

void
ext_mpi_alltoallv_init_native_ (void *sendbuf, int *sendcounts, int *sdispls,
				MPI_Datatype * sendtype, void *recvbuf,
				int *recvcounts, int *rdispls,
				MPI_Datatype * recvtype, MPI_Comm * comm_row,
				int *ext_mpi_cores_per_node_row,
				MPI_Comm * comm_column,
				int *ext_mpi_cores_per_node_column,
				int *num_active_ports, int *handle,
				int *chunks_throttle)
{
  *handle =
    EXT_MPI_Alltoallv_init_native (sendbuf, sendcounts, sdispls, *sendtype,
				   recvbuf, recvcounts, rdispls, *recvtype,
				   *comm_row, *ext_mpi_cores_per_node_row,
				   *comm_column,
				   *ext_mpi_cores_per_node_column,
				   *num_active_ports, *chunks_throttle);
}

void
ext_mpi_ialltoall_init_native_ (void *sendbuf, int *sendcount,
				MPI_Datatype * sendtype, void *recvbuf,
				int *recvcount, MPI_Datatype * recvtype,
				MPI_Comm * comm_row,
				int *ext_mpi_cores_per_node_row,
				MPI_Comm * comm_column,
				int *ext_mpi_cores_per_node_column,
				int *num_active_ports, int *handle_begin,
				int *handle_wait)
{
  EXT_MPI_Ialltoall_init_native (sendbuf, *sendcount, *sendtype, recvbuf,
				 *recvcount, *recvtype, *comm_row,
				 *ext_mpi_cores_per_node_row, *comm_column,
				 *ext_mpi_cores_per_node_column,
				 *num_active_ports, handle_begin,
				 handle_wait);
}

void
ext_mpi_ialltoallv_init_native_ (void *sendbuf, int *sendcounts, int *sdispls,
				 MPI_Datatype * sendtype, void *recvbuf,
				 int *recvcounts, int *rdispls,
				 MPI_Datatype * recvtype, MPI_Comm * comm_row,
				 int *ext_mpi_cores_per_node_row,
				 MPI_Comm * comm_column,
				 int *ext_mpi_cores_per_node_column,
				 int *num_active_ports, int *handle_begin,
				 int *handle_wait)
{
  EXT_MPI_Ialltoallv_init_native (sendbuf, sendcounts, sdispls, *sendtype,
				  recvbuf, recvcounts, rdispls, *recvtype,
				  *comm_row, *ext_mpi_cores_per_node_row,
				  *comm_column,
				  *ext_mpi_cores_per_node_column,
				  *num_active_ports, handle_begin,
				  handle_wait);
}

void
ext_mpi_alltoall_exec_native_ (int *handle, int *err)
{
  *err = EXT_MPI_Alltoall_exec_native (*handle);
}

void
ext_mpi_alltoall_done_native_ (int *handle, int *err)
{
  *err = EXT_MPI_Alltoall_done_native (*handle);
}

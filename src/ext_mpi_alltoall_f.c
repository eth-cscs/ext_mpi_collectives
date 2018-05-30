#include "ext_mpi_alltoall.h"

void
EXT_MPI_Alltoall_init_general_ (void *sendbuf, int *sendcount,
				MPI_Datatype * sendtype, void *recvbuf,
				int *recvcount, MPI_Datatype * recvtype,
				MPI_Comm * comm_row,
				int *my_cores_per_node_row,
				MPI_Comm * comm_column,
				int *my_cores_per_node_column, int *handle,
				int *ierr)
{
  *ierr =
    EXT_MPI_Alltoall_init_general (sendbuf, *sendcount, *sendtype, recvbuf,
				   *recvcount, *recvtype, *comm_row,
				   *my_cores_per_node_row, *comm_column,
				   *my_cores_per_node_column, handle);
}

void
EXT_MPI_Alltoallv_init_general_ (void *sendbuf, int *sendcounts, int *sdispls,
				 MPI_Datatype * sendtype, void *recvbuf,
				 int *recvcounts, int *rdispls,
				 MPI_Datatype * recvtype, MPI_Comm * comm_row,
				 int *my_cores_per_node_row,
				 MPI_Comm * comm_column,
				 int *my_cores_per_node_column, int *handle,
				 int *ierr)
{
  *ierr =
    EXT_MPI_Alltoallv_init_general (sendbuf, sendcounts, sdispls, *sendtype,
				    recvbuf, recvcounts, rdispls, *recvtype,
				    *comm_row, *my_cores_per_node_row,
				    *comm_column, *my_cores_per_node_column,
				    handle);
}

void
EXT_MPI_Ialltoall_init_general_ (void *sendbuf, int *sendcount,
				 MPI_Datatype * sendtype, void *recvbuf,
				 int *recvcount, MPI_Datatype * recvtype,
				 MPI_Comm * comm_row,
				 int *my_cores_per_node_row,
				 MPI_Comm * comm_column,
				 int *my_cores_per_node_column,
				 int *handle_begin, int *handle_wait,
				 int *ierr)
{
  *ierr =
    EXT_MPI_Ialltoall_init_general (sendbuf, *sendcount, *sendtype, recvbuf,
				    *recvcount, *recvtype, *comm_row,
				    *my_cores_per_node_row, *comm_column,
				    *my_cores_per_node_column, handle_begin,
				    handle_wait);
}

void
EXT_MPI_Ialltoallv_init_general_ (void *sendbuf, int *sendcounts,
				  int *sdispls, MPI_Datatype * sendtype,
				  void *recvbuf, int *recvcounts,
				  int *rdispls, MPI_Datatype * recvtype,
				  MPI_Comm * comm_row,
				  int *my_cores_per_node_row,
				  MPI_Comm * comm_column,
				  int *my_cores_per_node_column,
				  int *handle_begin, int *handle_wait,
				  int *ierr)
{
  *ierr =
    EXT_MPI_Ialltoallv_init_general (sendbuf, sendcounts, sdispls, *sendtype,
				     recvbuf, recvcounts, rdispls, *recvtype,
				     *comm_row, *my_cores_per_node_row,
				     *comm_column, *my_cores_per_node_column,
				     handle_begin, handle_wait);
}

void
EXT_MPI_Alltoall_init_ (void *sendbuf, int *sendcount,
			MPI_Datatype * sendtype, void *recvbuf,
			int *recvcount, MPI_Datatype * recvtype,
			MPI_Comm * comm, int *handle, int *ierr)
{
  *ierr =
    EXT_MPI_Alltoall_init (sendbuf, *sendcount, *sendtype, recvbuf,
			   *recvcount, *recvtype, *comm, handle);
}

void
EXT_MPI_Alltoallv_init_ (void *sendbuf, int *sendcounts, int *sdispls,
			 MPI_Datatype * sendtype, void *recvbuf,
			 int *recvcounts, int *rdispls,
			 MPI_Datatype * recvtype, MPI_Comm * comm,
			 int *handle, int *ierr)
{
  *ierr =
    EXT_MPI_Alltoallv_init (sendbuf, sendcounts, sdispls, *sendtype, recvbuf,
			    recvcounts, rdispls, *recvtype, *comm, handle);
}

void
EXT_MPI_Ialltoall_init_ (void *sendbuf, int *sendcount,
			 MPI_Datatype * sendtype, void *recvbuf,
			 int *recvcount, MPI_Datatype * recvtype,
			 MPI_Comm * comm, int *handle_begin, int *handle_wait,
			 int *ierr)
{
  *ierr =
    EXT_MPI_Ialltoall_init (sendbuf, *sendcount, *sendtype, recvbuf,
			    *recvcount, *recvtype, *comm, handle_begin,
			    handle_wait);
}

void
EXT_MPI_Ialltoallv_init_ (void *sendbuf, int *sendcounts, int *sdispls,
			  MPI_Datatype * sendtype, void *recvbuf,
			  int *recvcounts, int *rdispls,
			  MPI_Datatype * recvtype, MPI_Comm * comm,
			  int *handle_begin, int *handle_wait, int *ierr)
{
  *ierr =
    EXT_MPI_Ialltoallv_init (sendbuf, sendcounts, sdispls, *sendtype, recvbuf,
			     recvcounts, rdispls, *recvtype, *comm,
			     handle_begin, handle_wait);
}

void
EXT_MPI_Alltoall_exec_ (const void *sendbuf, int *sendcount,
			MPI_Datatype * sendtype, void *recvbuf,
			int *recvcount, MPI_Datatype * recvtype,
			MPI_Comm * comm, int *handle, int *ierr)
{
  *ierr =
    EXT_MPI_Alltoall_exec (sendbuf, *sendcount, *sendtype, recvbuf,
			   *recvcount, *recvtype, *comm, *handle);
}

void
EXT_MPI_Alltoallv_exec_ (const void *sendbuf, const int *sendcounts,
			 const int *sdispls, MPI_Datatype * sendtype,
			 void *recvbuf, const int *recvcounts,
			 const int *rdispls, MPI_Datatype * recvtype,
			 MPI_Comm * comm, int *handle, int *ierr)
{
  *ierr =
    EXT_MPI_Alltoallv_exec (sendbuf, sendcounts, sdispls, *sendtype, recvbuf,
			    recvcounts, rdispls, *recvtype, *comm, *handle);
}

void
EXT_MPI_Ialltoall_begin_ (const void *sendbuf, int *sendcount,
			  MPI_Datatype * sendtype, void *recvbuf,
			  int *recvcount, MPI_Datatype * recvtype,
			  MPI_Comm * comm, MPI_Request * request,
			  int *handle_begin, int *ierr)
{
  *ierr =
    EXT_MPI_Ialltoall_begin (sendbuf, *sendcount, *sendtype, recvbuf,
			     *recvcount, *recvtype, *comm, request,
			     *handle_begin);
}

void
EXT_MPI_Ialltoall_wait_ (MPI_Request * request, int *handle_wait, int *ierr)
{
  *ierr = EXT_MPI_Ialltoall_wait (request, *handle_wait);
}

void
EXT_MPI_Ialltoallv_begin_ (const void *sendbuf, const int *sendcounts,
			   const int *sdispls, MPI_Datatype * sendtype,
			   void *recvbuf, const int *recvcounts,
			   const int *rdispls, MPI_Datatype * recvtype,
			   MPI_Comm * comm, MPI_Request * request,
			   int *handle_begin, int *ierr)
{
  *ierr =
    EXT_MPI_Ialltoallv_begin (sendbuf, sendcounts, sdispls, *sendtype,
			      recvbuf, recvcounts, rdispls, *recvtype, *comm,
			      request, *handle_begin);
}

void
EXT_MPI_Ialltoallv_wait_ (MPI_Request * request, int *handle_wait, int *ierr)
{
  *ierr = EXT_MPI_Ialltoallv_wait (request, *handle_wait);
}

void
EXT_MPI_Alltoall_done_ (int *handle, int *ierr)
{
  *ierr = EXT_MPI_Alltoall_done (*handle);
}

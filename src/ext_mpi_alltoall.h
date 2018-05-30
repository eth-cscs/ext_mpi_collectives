#ifndef EXT_MPI_ALLTOALL_H_

#define EXT_MPI_ALLTOALL_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C"
{
#endif

  int EXT_MPI_Alltoall_init_general (void *sendbuf, int sendcount,
				     MPI_Datatype sendtype, void *recvbuf,
				     int recvcount, MPI_Datatype recvtype,
				     MPI_Comm comm_row,
				     int my_cores_per_node_row,
				     MPI_Comm comm_column,
				     int my_cores_per_node_column,
				     int *handle);
  int EXT_MPI_Alltoallv_init_general (void *sendbuf, int *sendcounts,
				      int *sdispls, MPI_Datatype sendtype,
				      void *recvbuf, int *recvcounts,
				      int *rdispls, MPI_Datatype recvtype,
				      MPI_Comm comm_row,
				      int my_cores_per_node_row,
				      MPI_Comm comm_column,
				      int my_cores_per_node_column,
				      int *handle);
  int EXT_MPI_Ialltoall_init_general (void *sendbuf, int sendcount,
				      MPI_Datatype sendtype, void *recvbuf,
				      int recvcount, MPI_Datatype recvtype,
				      MPI_Comm comm_row,
				      int my_cores_per_node_row,
				      MPI_Comm comm_column,
				      int my_cores_per_node_column,
				      int *handle_begin, int *handle_wait);
  int EXT_MPI_Ialltoallv_init_general (void *sendbuf, int *sendcounts,
				       int *sdispls, MPI_Datatype sendtype,
				       void *recvbuf, int *recvcounts,
				       int *rdispls, MPI_Datatype recvtype,
				       MPI_Comm comm_row,
				       int my_cores_per_node_row,
				       MPI_Comm comm_column,
				       int my_cores_per_node_column,
				       int *handle_begin, int *handle_wait);
  int EXT_MPI_Alltoall_init (void *sendbuf, int sendcount,
			     MPI_Datatype sendtype, void *recvbuf,
			     int recvcount, MPI_Datatype recvtype,
			     MPI_Comm comm, int *handle);
  int EXT_MPI_Alltoallv_init (void *sendbuf, int *sendcounts, int *sdispls,
			      MPI_Datatype sendtype, void *recvbuf,
			      int *recvcounts, int *rdispls,
			      MPI_Datatype recvtype, MPI_Comm comm,
			      int *handle);
  int EXT_MPI_Ialltoall_init (void *sendbuf, int sendcount,
			      MPI_Datatype sendtype, void *recvbuf,
			      int recvcount, MPI_Datatype recvtype,
			      MPI_Comm comm, int *handle_begin,
			      int *handle_wait);
  int EXT_MPI_Ialltoallv_init (void *sendbuf, int *sendcounts, int *sdispls,
			       MPI_Datatype sendtype, void *recvbuf,
			       int *recvcounts, int *rdispls,
			       MPI_Datatype recvtype, MPI_Comm comm,
			       int *handle_begin, int *handle_wait);
  int EXT_MPI_Alltoall_exec (const void *sendbuf, int sendcount,
			     MPI_Datatype sendtype, void *recvbuf,
			     int recvcount, MPI_Datatype recvtype,
			     MPI_Comm comm, int handle);
  int EXT_MPI_Alltoallv_exec (const void *sendbuf, const int *sendcounts,
			      const int *sdispls, MPI_Datatype sendtype,
			      void *recvbuf, const int *recvcounts,
			      const int *rdispls, MPI_Datatype recvtype,
			      MPI_Comm comm, int handle);
  int EXT_MPI_Ialltoall_begin (const void *sendbuf, int sendcount,
			       MPI_Datatype sendtype, void *recvbuf,
			       int recvcount, MPI_Datatype recvtype,
			       MPI_Comm comm, MPI_Request * request,
			       int handle_begin);
  int EXT_MPI_Ialltoall_wait (MPI_Request * request, int handle_wait);
  int EXT_MPI_Ialltoallv_begin (const void *sendbuf, const int *sendcounts,
				const int *sdispls, MPI_Datatype sendtype,
				void *recvbuf, const int *recvcounts,
				const int *rdispls, MPI_Datatype recvtype,
				MPI_Comm comm, MPI_Request * request,
				int handle_begin);
  int EXT_MPI_Ialltoallv_wait (MPI_Request * request, int handle_wait);
  int EXT_MPI_Alltoall_done (int handle);

#ifdef __cplusplus
}
#endif

#endif

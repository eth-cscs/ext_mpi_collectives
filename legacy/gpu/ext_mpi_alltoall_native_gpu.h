#ifndef EXT_MPI_ALLTOALL_NATIVE_GPU_H_

#define EXT_MPI_ALLTOALL_NATIVE_GPU_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C"
{
#endif

  int EXT_MPI_Alltoall_init_native_gpu (void *sendbuf, int sendcount,
				    MPI_Datatype sendtype, void *recvbuf,
				    int recvcount, MPI_Datatype recvtype,
				    MPI_Comm comm_row,
				    int my_cores_per_node_row,
				    MPI_Comm comm_column,
				    int my_cores_per_node_column,
				    int num_ports, int num_active_ports,
				    int chunks_throttle);
  int EXT_MPI_Alltoallv_init_native_gpu (void *sendbuf, int *sendcounts,
				     int *sdispls, MPI_Datatype sendtype,
				     void *recvbuf, int *recvcounts,
				     int *rdispls, MPI_Datatype recvtype,
				     MPI_Comm comm_row,
				     int my_cores_per_node_row,
				     MPI_Comm comm_column,
				     int my_cores_per_node_column,
				     int num_active_ports,
				     int chunks_throttle);
  void EXT_MPI_Ialltoall_init_native_gpu (void *sendbuf, int sendcount,
				      MPI_Datatype sendtype, void *recvbuf,
				      int recvcount, MPI_Datatype recvtype,
				      MPI_Comm comm_row,
				      int my_cores_per_node_row,
				      MPI_Comm comm_column,
				      int my_cores_per_node_column,
				      int num_active_ports, int *handle_begin,
				      int *handle_wait);
  void EXT_MPI_Ialltoallv_init_native_gpu (void *sendbuf, int *sendcounts,
				       int *sdispls, MPI_Datatype sendtype,
				       void *recvbuf, int *recvcounts,
				       int *rdispls, MPI_Datatype recvtype,
				       MPI_Comm comm_row,
				       int my_cores_per_node_row,
				       MPI_Comm comm_column,
				       int my_cores_per_node_column,
				       int num_active_ports,
				       int *handle_begin, int *handle_wait);
  int EXT_MPI_Alltoall_exec_native_gpu (int handle);
  int EXT_MPI_Alltoall_done_native_gpu (int handle);

#ifdef __cplusplus
}
#endif

#endif

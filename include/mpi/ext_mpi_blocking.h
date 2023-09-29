#ifndef EXT_MPI_BLOCKING_H_

#define EXT_MPI_BLOCKING_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int ext_mpi_num_tasks_per_node;
extern int ext_mpi_blocking;
extern int ext_mpi_bit_identical;
extern int ext_mpi_bit_reproducible;
extern int ext_mpi_minimum_computation;

int EXT_MPI_Init_blocking_comm(MPI_Comm comm, int i_comm);
int EXT_MPI_Finalize_blocking_comm(int i_comm);

int EXT_MPI_Allreduce(const void *sendbuf, void *recvbuf, int count, int reduction_op, int i_comm);
int EXT_MPI_Reduce_scatter_block(const void *sendbuf, void *recvbuf, int recvcount, int reduction_op, int i_comm);
int EXT_MPI_Allgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm, int i_comm);

#ifdef __cplusplus
}
#endif

#endif

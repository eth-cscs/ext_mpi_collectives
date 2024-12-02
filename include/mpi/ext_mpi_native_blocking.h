#ifndef EXT_MPI_NATIVE_BLOCKING_H_

#define EXT_MPI_NATIVE_BLOCKING_H_

#include <mpi.h>
#include "read_write.h"

#ifdef __cplusplus
extern "C" {
#endif

int EXT_MPI_Release_blocking_native(int i_comm);

int EXT_MPI_Allreduce_native(const void *sendbuf, void *recvbuf, int count, int reduction_op, int i_comm);
int EXT_MPI_Reduce_scatter_block_native(const void *sendbuf, void *recvbuf, int recvcount, int reduction_op, int i_comm);
int EXT_MPI_Allgather_native(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm, int i_comm);

#ifdef __cplusplus
}
#endif

#endif

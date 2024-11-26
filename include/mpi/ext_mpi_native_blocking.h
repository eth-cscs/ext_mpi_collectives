#ifndef EXT_MPI_NATIVE_BLOCKING_H_

#define EXT_MPI_NATIVE_BLOCKING_H_

#include <mpi.h>
#include "read_write.h"

enum collective_subtypes {
  out_of_place = 0,
  in_place = 1,
#ifndef GPU_ENABLED
  size = 2
#else
  out_of_place_gpu = 2,
  in_place_gpu = 3,
  size = 4
#endif
};

struct comm_properties {
  int count;
  int num_sockets_per_node;
  int copyin;
  int *num_ports;
  int *groups;
  int *copyin_factors;
};

#ifdef __cplusplus
extern "C" {
#endif

int EXT_MPI_Initialized_blocking_native(enum collective_subtypes collective_subtype, int i_comm);
int EXT_MPI_Release_blocking_native(int i_comm);

int EXT_MPI_Allreduce_native(const void *sendbuf, void *recvbuf, int count, int reduction_op, int i_comm);
int EXT_MPI_Reduce_scatter_block_native(const void *sendbuf, void *recvbuf, int recvcount, int reduction_op, int i_comm);
int EXT_MPI_Allgather_native(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm, int i_comm);

#ifdef __cplusplus
}
#endif

#endif

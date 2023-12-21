#ifndef EXT_MPI_COST_COPYIN_MEASUREMENT_H_

#define EXT_MPI_COST_COPYIN_MEASUREMENT_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

int EXT_MPI_Allreduce_measurement(const void *sendbuf, void *recvbuf, int count,
                                  MPI_Datatype datatype, MPI_Op op,
                                  MPI_Comm comm_row, int *my_cores_per_node_row,
                                  MPI_Comm comm_column, int my_cores_per_node_column,
				  int num_active_ports, int *copyin_method,
				  int **copyin_factors, int *num_sockets_per_node);

#ifdef __cplusplus
}
#endif

#endif

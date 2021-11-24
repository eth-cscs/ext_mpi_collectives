#ifndef EXT_MPI_H_

#define EXT_MPI_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int ext_mpi_bit_identical;
extern int use_rule_groups_ports;
extern int waitany;

int EXT_MPI_Init();
int EXT_MPI_Initialized(int *flag);
int EXT_MPI_Finalize();
int EXT_MPI_Allgatherv_init_general(void *sendbuf, int sendcount,
                                    MPI_Datatype sendtype, void *recvbuf,
                                    int *recvcounts, int *displs,
                                    MPI_Datatype recvtype, MPI_Comm comm_row,
                                    int my_cores_per_node_row,
                                    MPI_Comm comm_column,
                                    int my_cores_per_node_column, int *handle);
int EXT_MPI_Allgather_init_general(void *sendbuf, int sendcount,
                                   MPI_Datatype sendtype, void *recvbuf,
                                   int recvcount, MPI_Datatype recvtype,
                                   MPI_Comm comm_row, int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column, int *handle);
int EXT_MPI_Reduce_scatter_init_general(
    void *sendbuf, void *recvbuf, int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *handle);
int EXT_MPI_Reduce_scatter_block_init_general(
    void *sendbuf, void *recvbuf, int recvcount, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *handle);
int EXT_MPI_Allreduce_init_general(void *sendbuf, void *recvbuf, int count,
                                   MPI_Datatype datatype, MPI_Op op,
                                   MPI_Comm comm_row, int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column, int *handle);
int EXT_MPI_Bcast_init_general(void *buffer, int count, MPI_Datatype datatype,
                               int root, MPI_Comm comm_row,
                               int my_cores_per_node_row, MPI_Comm comm_column,
                               int my_cores_per_node_column, int *handle);
int EXT_MPI_Reduce_init_general(void *sendbuf, void *recvbuf, int count,
                                MPI_Datatype datatype, MPI_Op op, int root,
                                MPI_Comm comm_row, int my_cores_per_node_row,
                                MPI_Comm comm_column,
                                int my_cores_per_node_column, int *handle);
int EXT_MPI_Gatherv_init_general(void *sendbuf, int sendcount,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 int *recvcounts, int *displs,
                                 MPI_Datatype recvtype, int root,
                                 MPI_Comm comm_row, int my_cores_per_node_row,
                                 MPI_Comm comm_column,
                                 int my_cores_per_node_column, int *handle);
int EXT_MPI_Scatterv_init_general(void *sendbuf, int *sendcounts, int *displs,
                                  MPI_Datatype sendtype, void *recvbuf,
                                  int recvcount, MPI_Datatype recvtype,
                                  int root, MPI_Comm comm_row,
                                  int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column, int *handle);
int EXT_MPI_Allgatherv_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                            void *recvbuf, int *recvcounts, int *displs,
                            MPI_Datatype recvtype, MPI_Comm comm, int *handle);
int EXT_MPI_Allgather_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                           void *recvbuf, int recvcount, MPI_Datatype recvtype,
                           MPI_Comm comm, int *handle);
int EXT_MPI_Reduce_scatter_init(void *sendbuf, void *recvbuf, int *recvcounts,
                                MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
                                int *handle);
int EXT_MPI_Reduce_scatter_block_init(void *sendbuf, void *recvbuf,
                                      int recvcount, MPI_Datatype datatype,
                                      MPI_Op op, MPI_Comm comm, int *handle);
int EXT_MPI_Allreduce_init(void *sendbuf, void *recvbuf, int count,
                           MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
                           int *handle);
int EXT_MPI_Bcast_init(void *sendbuf, int count, MPI_Datatype datatype,
                       int root, MPI_Comm comm, int *handle);
int EXT_MPI_Reduce_init(void *sendbuf, void *recvbuf, int count,
                        MPI_Datatype datatype, MPI_Op op, int root,
                        MPI_Comm comm, int *handle);
int EXT_MPI_Exec(int handle);
int EXT_MPI_Test(int handle);
int EXT_MPI_Progress();
int EXT_MPI_Wait(int handle);
int EXT_MPI_Done(int handle);
int EXT_MPI_Allreduce_simulate(int count, MPI_Datatype datatype, MPI_Op op,
                               int comm_size_row, int my_cores_per_node_row,
                               int comm_size_column,
                               int my_cores_per_node_column);

#ifdef __cplusplus
}
#endif

#endif

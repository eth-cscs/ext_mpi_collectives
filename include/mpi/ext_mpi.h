#ifndef EXT_MPI_H_

#define EXT_MPI_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int ext_mpi_num_tasks_per_node;
extern int ext_mpi_blocking;
extern int ext_mpi_bit_identical;
extern int ext_mpi_bit_reproducible;
extern int ext_mpi_minimum_computation;

int EXT_MPI_Init();
int EXT_MPI_Initialized(int *flag);
int EXT_MPI_Finalize();
int EXT_MPI_Allgatherv_init_general(const void *sendbuf, int sendcount,
                                    MPI_Datatype sendtype, void *recvbuf,
                                    const int *recvcounts, const int *displs,
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
    const void *sendbuf, void *recvbuf, const int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *handle);
int EXT_MPI_Reduce_scatter_block_init_general(
    void *sendbuf, void *recvbuf, int recvcount, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *handle);
int EXT_MPI_Allreduce_init_general(const void *sendbuf, void *recvbuf, int count,
                                   MPI_Datatype datatype, MPI_Op op,
                                   MPI_Comm comm_row, int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column, int *handle);
int EXT_MPI_Bcast_init_general(void *buffer, int count, MPI_Datatype datatype,
                               int root, MPI_Comm comm_row,
                               int my_cores_per_node_row, MPI_Comm comm_column,
                               int my_cores_per_node_column, int *handle);
int EXT_MPI_Reduce_init_general(const void *sendbuf, void *recvbuf, int count,
                                MPI_Datatype datatype, MPI_Op op, int root,
                                MPI_Comm comm_row, int my_cores_per_node_row,
                                MPI_Comm comm_column,
                                int my_cores_per_node_column, int *handle);
int EXT_MPI_Gatherv_init_general(const void *sendbuf, int sendcount,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 const int *recvcounts, const int *displs,
                                 MPI_Datatype recvtype, int root,
                                 MPI_Comm comm_row, int my_cores_per_node_row,
                                 MPI_Comm comm_column,
                                 int my_cores_per_node_column, int *handle);
int EXT_MPI_Scatterv_init_general(const void *sendbuf, const int *sendcounts, const int *displs,
                                  MPI_Datatype sendtype, void *recvbuf,
                                  int recvcount, MPI_Datatype recvtype,
                                  int root, MPI_Comm comm_row,
                                  int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column, int *handle);
int EXT_MPI_Allgatherv_init(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                            void *recvbuf, const int *recvcounts, const int *displs,
                            MPI_Datatype recvtype, MPI_Comm comm, int *handle);
int EXT_MPI_Allgather_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                           void *recvbuf, int recvcount, MPI_Datatype recvtype,
                           MPI_Comm comm, int *handle);
int EXT_MPI_Reduce_scatter_init(const void *sendbuf, void *recvbuf, const int *recvcounts,
                                MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
                                int *handle);
int EXT_MPI_Reduce_scatter_block_init(void *sendbuf, void *recvbuf,
                                      int recvcount, MPI_Datatype datatype,
                                      MPI_Op op, MPI_Comm comm, int *handle);
int EXT_MPI_Allreduce_init(const void *sendbuf, void *recvbuf, int count,
                           MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
                           int *handle);
int EXT_MPI_Bcast_init(void *sendbuf, int count, MPI_Datatype datatype,
                       int root, MPI_Comm comm, int *handle);
int EXT_MPI_Reduce_init(const void *sendbuf, void *recvbuf, int count,
                        MPI_Datatype datatype, MPI_Op op, int root,
                        MPI_Comm comm, int *handle);
int EXT_MPI_Gatherv_init(const void *sendbuf, int sendcount,
                         MPI_Datatype sendtype, void *recvbuf,
                         const int *recvcounts, const int *displs,
                         MPI_Datatype recvtype, int root,
                         MPI_Comm comm, int *handle);
int EXT_MPI_Scatterv_init(const void *sendbuf, const int *sendcounts, const int *displs,
                          MPI_Datatype sendtype, void *recvbuf,
                          int recvcount, MPI_Datatype recvtype,
                          int root, MPI_Comm comm, int *handle);
int EXT_MPI_Start(int handle);
int EXT_MPI_Test(int handle);
int EXT_MPI_Progress();
int EXT_MPI_Wait(int handle);
int EXT_MPI_Done(int handle);

int EXT_MPI_Init_blocking_comm(MPI_Comm comm, int i_comm);
int EXT_MPI_Finalize_blocking_comm(int i_comm);

int EXT_MPI_Allreduce(const void *sendbuf, void *recvbuf, int count, int reduction_op, int i_comm);
int EXT_MPI_Reduce_scatter_block(const void *sendbuf, void *recvbuf, int recvcount, int reduction_op, int i_comm);
int EXT_MPI_Allgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm, int i_comm);

#ifdef __cplusplus
}
#endif

#endif

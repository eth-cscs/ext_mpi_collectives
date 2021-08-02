#ifndef EXT_MPI_ALLTOALL_H_

#define EXT_MPI_ALLTOALL_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

int EXT_MPI_Init();
int EXT_MPI_Initialized(int *flag);
int EXT_MPI_Finalize();
int EXT_MPI_Alltoall_init_general(void *sendbuf, int sendcount,
                                  MPI_Datatype sendtype, void *recvbuf,
                                  int recvcount, MPI_Datatype recvtype,
                                  MPI_Comm comm_row, int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column, int *handle);
int EXT_MPI_Alltoallv_init_general(void *sendbuf, int *sendcounts, int *sdispls,
                                   MPI_Datatype sendtype, void *recvbuf,
                                   int *recvcounts, int *rdispls,
                                   MPI_Datatype recvtype, MPI_Comm comm_row,
                                   int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column, int *handle);
int EXT_MPI_Ialltoall_init_general(void *sendbuf, int sendcount,
                                   MPI_Datatype sendtype, void *recvbuf,
                                   int recvcount, MPI_Datatype recvtype,
                                   MPI_Comm comm_row, int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column,
                                   int *handle_begin, int *handle_wait);
int EXT_MPI_Ialltoallv_init_general(
    void *sendbuf, int *sendcounts, int *sdispls, MPI_Datatype sendtype,
    void *recvbuf, int *recvcounts, int *rdispls, MPI_Datatype recvtype,
    MPI_Comm comm_row, int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *handle_begin, int *handle_wait);
int EXT_MPI_Alltoall_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                          void *recvbuf, int recvcount, MPI_Datatype recvtype,
                          MPI_Comm comm, int *handle);
int EXT_MPI_Alltoallv_init(void *sendbuf, int *sendcounts, int *sdispls,
                           MPI_Datatype sendtype, void *recvbuf,
                           int *recvcounts, int *rdispls, MPI_Datatype recvtype,
                           MPI_Comm comm, int *handle);
int EXT_MPI_Ialltoall_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                           void *recvbuf, int recvcount, MPI_Datatype recvtype,
                           MPI_Comm comm, int *handle_begin, int *handle_wait);
int EXT_MPI_Ialltoallv_init(void *sendbuf, int *sendcounts, int *sdispls,
                            MPI_Datatype sendtype, void *recvbuf,
                            int *recvcounts, int *rdispls,
                            MPI_Datatype recvtype, MPI_Comm comm,
                            int *handle_begin, int *handle_wait);
int EXT_MPI_Scatter_init_general(void *sendbuf, int sendcount,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 int recvcount, MPI_Datatype recvtype, int root,
                                 MPI_Comm comm_row, int my_cores_per_node_row,
                                 MPI_Comm comm_column,
                                 int my_cores_per_node_column, int *handle);
int EXT_MPI_Gather_init_general(void *sendbuf, int sendcount,
                                MPI_Datatype sendtype, void *recvbuf,
                                int recvcount, MPI_Datatype recvtype, int root,
                                MPI_Comm comm_row, int my_cores_per_node_row,
                                MPI_Comm comm_column,
                                int my_cores_per_node_column, int *handle);
int EXT_MPI_Scatter_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                         void *recvbuf, int recvcount, MPI_Datatype recvtype,
                         int root, MPI_Comm comm, int *handle);
int EXT_MPI_Gather_init(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                        void *recvbuf, int recvcount, MPI_Datatype recvtype,
                        int root, MPI_Comm comm, int *handle);
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
int EXT_MPI_Bcast_init_general(void *sendbuf, int count, MPI_Datatype datatype,
                               int root, MPI_Comm comm_row,
                               int my_cores_per_node_row, MPI_Comm comm_column,
                               int my_cores_per_node_column, int *handle);
int EXT_MPI_Reduce_init_general(void *sendbuf, void *recvbuf, int count,
                                MPI_Datatype datatype, MPI_Op op, int root,
                                MPI_Comm comm_row, int my_cores_per_node_row,
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
int EXT_MPI_Done(int handle);
int EXT_MPI_Allocate(MPI_Comm comm_row, int my_cores_per_node_row,
                     MPI_Comm comm_column, int my_cores_per_node_column,
                     int sharedmem_size, int locmem_size_);
int EXT_MPI_Init_general(MPI_Comm comm_row, int my_cores_per_node_row,
                         MPI_Comm comm_column, int my_cores_per_node_column,
                         int *handle);
int EXT_MPI_Done_general(int handle);
int EXT_MPI_Allgatherv(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                       void *recvbuf, int *recvcounts, int *displs,
                       MPI_Datatype recvtype, int handle);
int EXT_MPI_Allgather(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                      void *recvbuf, int recvcount, MPI_Datatype recvtype,
                      int handle);
int EXT_MPI_Reduce_scatter(void *sendbuf, void *recvbuf, int *recvcounts,
                           MPI_Datatype datatype, MPI_Op op, int handle);
int EXT_MPI_Reduce_scatter_block(void *sendbuf, void *recvbuf, int recvcount,
                                 MPI_Datatype datatype, MPI_Op op, int handle);
int EXT_MPI_Allreduce(void *sendbuf, void *recvbuf, int count,
                      MPI_Datatype datatype, MPI_Op op, int handle);
int EXT_MPI_Bcast(void *sendbuf, int count, MPI_Datatype datatype, int root,
                  int handle);
int EXT_MPI_Reduce(void *sendbuf, void *recvbuf, int count,
                   MPI_Datatype datatype, MPI_Op op, int root, int handle);
int EXT_MPI_Dry_allreduce(int mpi_size, int count, int *handle);

#ifdef __cplusplus
}
#endif

#endif

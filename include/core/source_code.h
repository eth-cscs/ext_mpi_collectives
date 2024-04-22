#ifndef EXT_MPI_SOURCE_CODE_H_

#define EXT_MPI_SOURCE_CODE_H_

#ifdef __cplusplus
extern "C" {
#endif

int ext_mpi_generate_source_code(char **shmem,
                               int *shmemid, int *shmem_sizes,
                               char *buffer_in, char **sendbufs, char **recvbufs,
                               int barriers_size, char *locmem,
                               int reduction_op, void *func, int *global_ranks,
                               char *code_out, int size_comm, int size_request, void *comm_row,
                               int node_num_cores_row, void *comm_column,
                               int node_num_cores_column,
                               int *shmemid_gpu, char **shmem_gpu, int *gpu_byte_code_counter, int tag, int id);

#ifdef __cplusplus
}
#endif

#endif

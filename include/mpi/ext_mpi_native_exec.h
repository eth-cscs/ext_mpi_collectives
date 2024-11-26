#ifndef EXT_MPI_NATIVE_EXEC_H_

#define EXT_MPI_NATIVE_EXEC_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

int ext_mpi_exec_native(char *ip, char **ip_exec, int active_wait);
int ext_mpi_exec_blocking(char *ip, int tag, char **shmem_socket, int *counter_socket, int socket_rank, int num_cores, char **shmem_node, int *counter_node, int num_sockets_per_node, void **shmem_blocking, void **sendbuf, void **recvbuf, int count, int reduction_op, int count_io, void *p_dev_temp);
int ext_mpi_normalize_blocking(char *ip, MPI_Comm comm, int tag, int count, char **send_pointers_allreduce_blocking);
int ext_mpi_exec_padding(char *ip, void *sendbuf, void *recvbuf, void **shmem, int *numbers_padding);
int EXT_MPI_Allreduce_to_disc(char *ip, char *locmem_blocking, int *rank_list, char *raw_code);
char * EXT_MPI_Allreduce_from_disc(int csize, char *raw_code, char *locmem_blocking, int *rank_list);

#ifdef __cplusplus
}
#endif

#ifdef __x86_64__
#define memory_fence() asm volatile("mfence" :: \
                                        : "memory")
#define memory_fence_load() asm volatile("lfence" :: \
                                      : "memory")
#define memory_fence_store() asm volatile("sfence" :: \
                                       : "memory")
#else
#define memory_fence() __atomic_thread_fence(__ATOMIC_ACQ_REL)
#define memory_fence_load() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define memory_fence_store() __atomic_thread_fence(__ATOMIC_RELEASE)
#endif

#endif

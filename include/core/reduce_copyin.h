#ifndef EXT_MPI_REDUCE_COPYIN_H_

#define EXT_MPI_REDUCE_COPYIN_H_

#ifdef __cplusplus
extern "C"
{
#endif

int ext_mpi_generate_reduce_copyin(char *buffer_in, char *buffer_out);
int ext_mpi_generate_allreduce_copyin(char *buffer_in, char *buffer_out);
int ext_mpi_generate_allreduce_copyout(char *buffer_in, char *buffer_out);
int ext_mpi_generate_allreduce_copyin_shmem(char *buffer_in, char *buffer_out);
int ext_mpi_generate_allreduce_copyout_shmem(char *buffer_in, char *buffer_out);
void ext_mpi_rank_order(int size, int num_factors, int *factors, int *ranks);

#ifdef __cplusplus
}
#endif

#endif

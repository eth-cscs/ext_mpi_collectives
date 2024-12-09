#ifndef EXT_MPI_MEMORY_MANAGER_H_

#define EXT_MPI_MEMORY_MANAGER_H_

#ifdef __cplusplus
extern "C"
{
#endif

void ** ext_mpi_get_shmem_root_cpu();
int ext_mpi_dmalloc_init(void **root, void *memory_chunk, size_t max_bytes);
void ext_mpi_dmalloc_done(void *root);
void* ext_mpi_dmalloc(void *root, size_t numbytes);
void ext_mpi_dfree(void *root, void* ptr);

#ifdef __cplusplus
}
#endif

#endif

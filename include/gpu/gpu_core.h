#ifndef GPU_CORE_H_

#define GPU_CORE_H_

#ifdef __cplusplus
extern "C"
{
#endif

  void ext_mpi_gpu_malloc(void **p, int size);
  void ext_mpi_gpu_free(void *p);
  void ext_mpi_gpu_memcpy_hd(void *dest, void *src, int length);
  void ext_mpi_gpu_memcpy_dh(void *dest, void *src, int length);
  int ext_mpi_gpu_is_device_pointer(const void *ptr);
  void ext_mpi_gpu_synchronize();
  void ext_mpi_gpu_copy_reduce(char instruction2, void *data, int count);

#ifdef __cplusplus
}
#endif

#endif

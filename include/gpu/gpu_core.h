#ifndef GPU_CORE_H_

#define GPU_CORE_H_

#ifdef __cplusplus
extern "C"
{
#endif

  void gpu_malloc(void **p, int size);
  void gpu_free(void *p);
  void gpu_memcpy_hd(void *dest, void *src, int length);
  void gpu_memcpy_dh(void *dest, void *src, int length);
  int gpu_is_device_pointer(const void *ptr);
  void gpu_synchronize();
  void gpu_copy_reduce(char instruction2, void *data, int count);

#ifdef __cplusplus
}
#endif

#endif

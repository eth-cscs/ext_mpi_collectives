#include "constants.h"
#include "gpu_core.h"
#include <cuda.h>
#include <stdio.h>

void ext_mpi_gpu_malloc(void **p, int size) {
  if (cudaMalloc(p, size) != cudaSuccess) {
    printf("error in gpu_malloc\n");
    exit(13);
  }
#ifdef DEBUG
  if (cudaMemset(*p, -1, size) != cudaSuccess) {
    printf("error in gpu_malloc\n");
    exit(13);
  }
#endif
}

void ext_mpi_gpu_free(void *p) {
  if (p){
    if (cudaFree(p) != cudaSuccess) {
      printf("error in gpu_free\n");
      exit(13);
    }
  }
}

void ext_mpi_gpu_memcpy_hd(void *dest, void *src, int length) {
  if (cudaMemcpy(dest, src, length, cudaMemcpyHostToDevice) != cudaSuccess) {
    printf("error in gpu_memcpy_hd\n");
    exit(13);
  }
}

void ext_mpi_gpu_memcpy_dh(void *dest, void *src, int length) {
  if (cudaMemcpy(dest, src, length, cudaMemcpyDeviceToHost) != cudaSuccess) {
    printf("error in gpu_memcpy_dh\n");
    exit(13);
  }
}

int ext_mpi_gpu_is_device_pointer(const void *ptr) {
  struct cudaPointerAttributes attributes;
  cudaPointerGetAttributes(&attributes, ptr);
  return (attributes.devicePointer != NULL);
}

void ext_mpi_gpu_synchronize() {
  if (cudaDeviceSynchronize() != cudaSuccess) {
    printf(" cudaError gpu_synchronize\n");
    exit(13);
  }
}

template <typename vartype> __global__ void gpu_copy_reduce_kernel(char *data) {
  int num_streams, index, offset, num_stream, i;
  long int max_size, size;
  char *ldata, *p1, *p2;
  num_streams = *((int *)(data + sizeof(int)));
  max_size = *((long int *)(data + 2 * sizeof(int)));

  for (i = blockIdx.x * blockDim.x + threadIdx.x; i < num_streams * max_size;
       i += blockDim.x * gridDim.x) {
    num_stream = i / max_size;
    offset = i % max_size;
    index = 0;
    ldata = data + 2 * sizeof(int) + sizeof(long int) +
            (num_streams * index + num_stream) *
                (sizeof(char *) * 2 + sizeof(long int));
    p1 = *((char **)ldata);
    while (p1) {
      p2 = *((char **)(ldata + sizeof(char *)));
      size = *((long int *)(ldata + 2 * sizeof(char *)));
      if (size >= 0) {
        if (offset < size) {
          ((vartype *)p1)[offset] = ((vartype *)p2)[offset];
        }
      } else {
        if (offset < -size) {
          ((vartype *)p1)[offset] += ((vartype *)p2)[offset];
        }
      }
      index++;
      ldata = data + 2 * sizeof(int) + sizeof(long int) +
              (num_streams * index + num_stream) *
                  (sizeof(char *) * 2 + sizeof(long int));
      p1 = *((char **)ldata);
    }
  }
}

void ext_mpi_gpu_copy_reduce(char instruction2, void *data, int count) {
  switch (instruction2) {
  case OPCODE_REDUCE_SUM_CHAR:
    gpu_copy_reduce_kernel<char><<<(count + 127) / 128, 128>>>((char *)data);
    break;
  case OPCODE_REDUCE_SUM_DOUBLE:
    gpu_copy_reduce_kernel<double><<<(count + 127) / 128, 128>>>((char *)data);
    break;
  case OPCODE_REDUCE_SUM_LONG_INT:
    gpu_copy_reduce_kernel<long int>
        <<<(count + 127) / 128, 128>>>((char *)data);
    break;
  case OPCODE_REDUCE_SUM_FLOAT:
    gpu_copy_reduce_kernel<float><<<(count + 127) / 128, 128>>>((char *)data);
    break;
  case OPCODE_REDUCE_SUM_INT:
    gpu_copy_reduce_kernel<int><<<(count + 127) / 128, 128>>>((char *)data);
    break;
  }
}

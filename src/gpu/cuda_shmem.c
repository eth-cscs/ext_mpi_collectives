#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dlfcn.h>
#include <assert.h>
#include <cuda.h>
#include "gpu_shmem.h"
#include "memory_manager.h"
#include "shmem.h"
#include "ext_mpi.h"
#include "ext_mpi_native.h"
#include "ext_mpi_native_exec.h"
#include "avltree.h"

extern MPI_Comm ext_mpi_COMM_WORLD_dup;
//static void **shmem_root_cpu = NULL;
static int mpi_size_node_global = -1;
static int mpi_rank_node_global = -1;
static int *sizes_shared_global = NULL;
static int *shmemid_global = NULL;
static char **shmem_global = NULL;
static long int *shmem_offsets = NULL;
static void *shmem_root_gpu = NULL;

void ** ext_mpi_get_shmem_root_gpu() {
  return &shmem_root_gpu;
}

int ext_mpi_gpu_sizeof_memhandle() { return (sizeof(struct cudaIpcMemHandle_st)); }

void *ext_mpi_init_shared_memory_gpu(MPI_Comm comm_world, size_t size_shared) {
  struct cudaIpcMemHandle_st shmemid_gpu;
  int my_mpi_rank, my_mpi_size, my_cores_per_node, i, j, flag;
  MPI_Comm shmem_comm_node;
  my_cores_per_node = ext_mpi_get_num_tasks_per_socket(comm_world, 1);
  if (my_cores_per_node <= 1) {
    return NULL;
  }
  ext_mpi_call_mpi(MPI_Comm_size(comm_world, &my_mpi_size));
  ext_mpi_call_mpi(MPI_Comm_rank(comm_world, &my_mpi_rank));
  ext_mpi_call_mpi(PMPI_Comm_split(comm_world, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &shmem_comm_node));
  ext_mpi_call_mpi(MPI_Comm_size(shmem_comm_node, &mpi_size_node_global));
  ext_mpi_call_mpi(MPI_Comm_rank(shmem_comm_node, &mpi_rank_node_global));
  if (size_shared > 0) {
    size_shared = (((size_shared - 1) / CACHE_LINE_SIZE) + 1) * CACHE_LINE_SIZE;
  }

  shmem_global = (char **)malloc(my_cores_per_node * sizeof(char *));
  shmemid_global = (int *)malloc(my_cores_per_node * sizeof(int));
  if ((shmem_global == NULL) || (shmemid_global == NULL)) {
    exit(14);
  }
  memset(shmem_global, 0, my_cores_per_node * sizeof(char *));
  memset(shmemid_global, 0, my_cores_per_node * sizeof(int));
  for (i = 0; i < my_cores_per_node; i++) {
    memset(&shmemid_gpu, 0, sizeof(struct cudaIpcMemHandle_st));
    if (my_mpi_rank == i) {
      if (!size_shared) {
	shmem_global[i] = NULL;
	shmemid_global[i] = 1;
      } else {
        if (cudaMalloc((void *)&(shmem_global[i]), size_shared) != 0) {
	  printf("error cudaMalloc in file cuda_shmem.c\n");
          exit(16);
	}
        if (shmem_global[i] == NULL)
          exit(16);
        if (cudaIpcGetMemHandle(&shmemid_gpu, (void *)(shmem_global[i])) != 0)
          exit(15);
        shmemid_global[i] |= 1;
      }
    }
    ext_mpi_call_mpi(PMPI_Bcast(&shmemid_gpu, sizeof(struct cudaIpcMemHandle_st), MPI_CHAR, i, shmem_comm_node));
    ext_mpi_call_mpi(PMPI_Barrier(shmem_comm_node));
    flag = 0;
    for (j = 0; j < sizeof(struct cudaIpcMemHandle_st); j++) {
      if (((char *)(&shmemid_gpu))[j]) flag = 1;
    }
    if (flag) {
      if (shmem_global[i] == NULL) {
        if (cudaIpcOpenMemHandle((void **)&shmem_global[i], shmemid_gpu,
				 cudaIpcMemLazyEnablePeerAccess) != 0)
	  exit(13);
	shmemid_global[i] |= 2;
      }
      if (shmem_global[i] == NULL)
	exit(2);
    } else {
      shmem_global[i] = NULL;
      shmemid_global[i] |= 2;
    }
    ext_mpi_call_mpi(PMPI_Barrier(shmem_comm_node));
  }
  shmem_offsets = (long int *)malloc(my_cores_per_node * sizeof(long int));
  ext_mpi_call_mpi(PMPI_Allgather(&shmem_global[mpi_rank_node_global], 1, MPI_LONG, shmem_offsets, 1, MPI_LONG, shmem_comm_node));
  for (i = 0; i < my_cores_per_node; i++) {
    shmem_offsets[i] = shmem_global[i] - (char*)(shmem_offsets[i]);
  }
  sizes_shared_global = (int*)malloc(my_cores_per_node * sizeof(int));
  ext_mpi_call_mpi(PMPI_Allgather(&size_shared, 1, MPI_INT, sizes_shared_global, 1, MPI_INT, shmem_comm_node));
  ext_mpi_call_mpi(PMPI_Comm_free(&shmem_comm_node));
  return shmem_global[mpi_rank_node_global];
}

int ext_mpi_done_shared_memory_gpu(MPI_Comm comm_world) {
  int i;
  for (i = 0; i < mpi_size_node_global; i++) {
    if ((shmemid_global[i] & 2) && shmem_global[i]) {
      if (cudaIpcCloseMemHandle((void *)(shmem_global[i])) != 0) {
        exit(13);
      }
    }
  }
  for (i = 0; i < mpi_size_node_global; i++) {
    if ((shmemid_global[i] & 1) && shmem_global[i]) {
      if (cudaFree((void *)(shmem_global[i])) != 0) {
        exit(13);
      }
    }
  }
  free(shmem_global);
  free(shmemid_global);
  return 0;
}

int ext_mpi_gpu_setup_shared_memory(MPI_Comm comm, int my_cores_per_socket,
                                    int size_shared, int num_sockets_per_node,
                                    int **shmemidi_gpu, char ***shmem_gpu) {
  MPI_Comm shmem_comm_node;
  int my_mpi_rank, my_mpi_size, shmemid_temp, *ranks_global, i, j, k;
  long int *offsets, offset;
  char *shmem_temp;
  ext_mpi_call_mpi(MPI_Comm_size(comm, &my_mpi_size));
  ext_mpi_call_mpi(MPI_Comm_rank(comm, &my_mpi_rank));
  ext_mpi_call_mpi(PMPI_Comm_split(comm, my_mpi_rank / (my_cores_per_socket * num_sockets_per_node),
                  my_mpi_rank % (my_cores_per_socket * num_sockets_per_node), &shmem_comm_node));
  ext_mpi_call_mpi(MPI_Comm_rank(shmem_comm_node, &my_mpi_rank));
  *shmem_gpu = (char **)malloc(my_cores_per_socket * num_sockets_per_node * sizeof(char *));
  *shmemidi_gpu = (int *)malloc(my_cores_per_socket * num_sockets_per_node * sizeof(int));
  if ((*shmem_gpu == NULL) || (*shmemidi_gpu == NULL)) {
    exit(14);
  }
  memset(*shmem_gpu, 0, my_cores_per_socket * num_sockets_per_node * sizeof(char *));
  memset(*shmemidi_gpu, 0, my_cores_per_socket * num_sockets_per_node * sizeof(int));
  ranks_global = (int *)malloc(my_cores_per_socket * num_sockets_per_node * sizeof(int));
  ext_mpi_call_mpi(PMPI_Allgather(&mpi_rank_node_global, 1, MPI_INT, ranks_global, 1, MPI_INT, shmem_comm_node));
  i = my_mpi_rank % (my_cores_per_socket * num_sockets_per_node);
  (*shmemidi_gpu)[i] = -1;
  (*shmem_gpu)[i] = ext_mpi_dmalloc(shmem_root_gpu, size_shared);
  if (!(*shmem_gpu)[i]) {
    printf("dalloc: not enough shared memory\n");
    exit(1);
  }
  offset = (*shmem_gpu)[i] - shmem_global[mpi_rank_node_global];
  offsets = (long int*)malloc(my_cores_per_socket * num_sockets_per_node * sizeof(long int));
  ext_mpi_call_mpi(PMPI_Allgather(&offset, 1, MPI_LONG, offsets, 1, MPI_LONG, shmem_comm_node));
  for (i = 0; i < my_cores_per_socket * num_sockets_per_node; i++) {
    (*shmem_gpu)[i] = shmem_global[ranks_global[i]] + offsets[i];
  }
  free(offsets);
  free(ranks_global);
  ext_mpi_call_mpi(PMPI_Comm_free(&shmem_comm_node));
  for (j = 0; j < (my_mpi_rank % (my_cores_per_socket * num_sockets_per_node)) / my_cores_per_socket * my_cores_per_socket; j++) {
    shmem_temp = (*shmem_gpu)[0];
    shmemid_temp = (*shmemidi_gpu)[0];
    for (i = 0; i < my_cores_per_socket * num_sockets_per_node - 1; i++) {
      (*shmem_gpu)[i] = (*shmem_gpu)[i + 1];
      (*shmemidi_gpu)[i] = (*shmemidi_gpu)[i + 1];
    }
    (*shmem_gpu)[my_cores_per_socket * num_sockets_per_node - 1] = shmem_temp;
    (*shmemidi_gpu)[my_cores_per_socket * num_sockets_per_node - 1] = shmemid_temp;
  }
  for (k = 0; k < num_sockets_per_node; k++) {
    for (j = 0; j < my_mpi_rank % my_cores_per_socket; j++) {
      shmem_temp = (*shmem_gpu)[my_cores_per_socket * k];
      shmemid_temp = (*shmemidi_gpu)[my_cores_per_socket * k];
      for (i = my_cores_per_socket * k; i < my_cores_per_socket * (k + 1) - 1; i++) {
        (*shmem_gpu)[i] = (*shmem_gpu)[i + 1];
        (*shmemidi_gpu)[i] = (*shmemidi_gpu)[i + 1];
      }
      (*shmem_gpu)[my_cores_per_socket * (k + 1) - 1] = shmem_temp;
      (*shmemidi_gpu)[my_cores_per_socket * (k + 1) - 1] = shmemid_temp;
    }
  }
  return 0;
}

int ext_mpi_gpu_destroy_shared_memory(int my_cores_per_node, int *ranks_node, int *shmemid_gpu, char **shmem_gpu) {
  MPI_Comm newcomm;
  MPI_Group world_group, group;
  int *ranks_node_l, i;
  if (my_cores_per_node > 1 && ranks_node) {
    ranks_node_l = (int*)malloc(my_cores_per_node * sizeof(int));
    for (i = 0; i < my_cores_per_node; i++) {
      ranks_node_l[i] = ranks_node[i];
    }
    ext_mpi_call_mpi(PMPI_Comm_group(ext_mpi_COMM_WORLD_dup, &world_group));
    ext_mpi_call_mpi(PMPI_Group_incl(world_group, my_cores_per_node, ranks_node_l, &group));
    ext_mpi_call_mpi(PMPI_Comm_create_group(ext_mpi_COMM_WORLD_dup, group, 0, &newcomm));
    ext_mpi_call_mpi(PMPI_Barrier(newcomm));
    ext_mpi_call_mpi(PMPI_Comm_free(&newcomm));
    ext_mpi_call_mpi(PMPI_Group_free(&group));
    ext_mpi_call_mpi(PMPI_Group_free(&world_group));
    free(ranks_node_l);
  }
  for (i = 0; i < my_cores_per_node; i++) {
    if (shmemid_gpu[i] == -1) {
      ext_mpi_dfree(shmem_root_gpu, shmem_gpu[i]);
    }
  }
  free(shmemid_gpu);
  free(shmem_gpu);
  return 0;
}

int ext_mpi_sendrecvbuf_init_gpu(MPI_Comm comm, int my_cores_per_node, int num_sockets, char *sendrecvbuf, int size, char ***sendrecvbufs, int *mem_partners) {
  MPI_Comm gpu_comm_node;
  struct cudaIpcMemHandle_st shmemid_gpu_l, *shmemid_gpu;
  int my_mpi_rank, my_mpi_size, i, j, k, *mem_partners_l = NULL, temp;
  char *a;
  if (!sendrecvbuf) {
    *sendrecvbufs = NULL;
    return -1;
  }
  ext_mpi_call_mpi(PMPI_Comm_rank(comm, &my_mpi_rank));
  ext_mpi_call_mpi(PMPI_Comm_size(comm, &my_mpi_size));
  ext_mpi_call_mpi(PMPI_Comm_split(comm, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &gpu_comm_node));
  ext_mpi_call_mpi(PMPI_Comm_rank(gpu_comm_node, &my_mpi_rank));
  ext_mpi_call_mpi(PMPI_Comm_size(gpu_comm_node, &my_mpi_size));
  shmemid_gpu = (struct cudaIpcMemHandle_st*)malloc(sizeof(struct cudaIpcMemHandle_st) * my_mpi_size);
  *sendrecvbufs = (char **)malloc(my_mpi_size * sizeof(char *));
  ext_mpi_call_mpi(PMPI_Allreduce(MPI_IN_PLACE, &size, 1, MPI_INT, MPI_MAX, gpu_comm_node));
  if (mem_partners) {
    mem_partners_l = (int*)malloc(sizeof(int) * my_mpi_size);
    memset(mem_partners_l, 0, sizeof(int) * my_mpi_size);
    for (i = 0; mem_partners[i] >= 0; i++) {
      mem_partners_l[mem_partners[i]] = 1;
    }
    for (k = 0; k < num_sockets; k++) {
      for (j = 0; j < my_mpi_rank % (my_mpi_size / num_sockets); j++) {
        temp = mem_partners_l[(my_mpi_size / num_sockets) * (k + 1) - 1];
        for (i = (my_mpi_size / num_sockets) * (k + 1) - 2; i >= (my_mpi_size / num_sockets) * k; i--) {
          mem_partners_l[i + 1] = mem_partners_l[i];
        }
        mem_partners_l[(my_mpi_size / num_sockets) * k] = temp;
      }
    }
    for (j = 0; j < my_mpi_rank / (my_mpi_size / num_sockets) * (my_mpi_size / num_sockets); j++) {
      temp = mem_partners_l[my_mpi_size - 1];
      for (i = my_mpi_size - 2; i >= 0; i--) {
        mem_partners_l[i + 1] = mem_partners_l[i];
      }
      mem_partners_l[0] = temp;
    }
  }
  if (cudaIpcGetMemHandle(&shmemid_gpu_l, (void *)sendrecvbuf) != 0) {
    printf("error cudaIpcGetMemHandle in cuda_shmem.c\n");
    exit(1);
  }
  ext_mpi_call_mpi(PMPI_Allgather(&shmemid_gpu_l, sizeof(struct cudaIpcMemHandle_st), MPI_CHAR, shmemid_gpu, sizeof(struct cudaIpcMemHandle_st), MPI_CHAR, gpu_comm_node));
  for (i = 0; i < my_mpi_size; i++) {
    if (i == my_mpi_rank) {
      (*sendrecvbufs)[i] = sendrecvbuf;
    } else {
      if (!mem_partners_l || (mem_partners_l && mem_partners_l[i])) {
        if (cudaIpcOpenMemHandle((void **)&((*sendrecvbufs)[i]), shmemid_gpu[i], cudaIpcMemLazyEnablePeerAccess) != 0) {
	  printf("error 1 cudaIpcOpenMemHandle in cuda_shmem.c\n");
	  exit(1);
        }
      } else {
	(*sendrecvbufs)[i] = NULL;
      }
    }
  }
  free(mem_partners_l);
  free(shmemid_gpu);
  for (j = 0; j < my_mpi_rank / (my_mpi_size / num_sockets) * (my_mpi_size / num_sockets); j++) {
    a = (*sendrecvbufs)[0];
    for (i = 0; i < my_mpi_size - 1; i++) {
      (*sendrecvbufs)[i] = (*sendrecvbufs)[i + 1];
    }
    (*sendrecvbufs)[my_mpi_size - 1] = a;
  }
  for (k = 0; k < num_sockets; k++) {
    for (j = 0; j < my_mpi_rank % (my_mpi_size / num_sockets); j++) {
      a = (*sendrecvbufs)[(my_mpi_size / num_sockets) * k];
      for (i = (my_mpi_size / num_sockets) * k; i < (my_mpi_size / num_sockets) * (k + 1) - 1; i++) {
	(*sendrecvbufs)[i] = (*sendrecvbufs)[i + 1];
      }
      (*sendrecvbufs)[(my_mpi_size / num_sockets) * (k + 1) - 1] = a;
    }
  }
  (*sendrecvbufs)[0] = sendrecvbuf;
  ext_mpi_call_mpi(PMPI_Comm_free(&gpu_comm_node));
  return 0;
}

int ext_mpi_sendrecvbuf_done_gpu(int my_cores_per_node, char **sendrecvbufs) {
  int i;
  char *addr;
  for (i = 1; i < my_cores_per_node; i++) {
    addr = sendrecvbufs[i];
    if (addr) {
      assert(cudaIpcCloseMemHandle((void *)(addr)) == 0);
    }
  }
  free(sendrecvbufs);
  return 0;
}

struct address_node {
  struct avl_node avl;
  char *address_key;
  size_t size;
  struct cudaIpcMemHandle_st cuda_mem_handle;
  char **address_values;
};

static int cmp_func(struct avl_node *a, struct avl_node *b, void *aux)
{
    struct address_node *aa, *bb;
    aa = _get_entry(a, struct address_node, avl);
    bb = _get_entry(b, struct address_node, avl);

    if (aa->address_key < bb->address_key) return -1;
    else if (aa->address_key > bb->address_key) return 1;
    else return 0;
}

static int mpi_node_rank = -1, mpi_node_size = -1;
static CUresult CUDAAPI(*sys_cuMemFree) (CUdeviceptr dptr);
static cudaError_t CUDARTAPI(*sys_cudaFree) (void *dptr);
static long int *shmem_offsets;
static struct avl_tree address_lookup_root;

static int gpu_mem_hook_init()
{
    void *libcuda_handle;
    void *libcudart_handle;

    libcuda_handle = dlopen("libcuda.so", RTLD_LAZY | RTLD_GLOBAL);
    assert(libcuda_handle);
    libcudart_handle = dlopen("libcudart.so", RTLD_LAZY | RTLD_GLOBAL);
    assert(libcudart_handle);

    sys_cuMemFree = (void *) dlsym(libcuda_handle, "cuMemFree");
    assert(sys_cuMemFree);
    sys_cudaFree = (void *) dlsym(libcudart_handle, "cudaFree");
    assert(sys_cudaFree);

    return 0;
}

CUresult CUDAAPI cuMemFree(CUdeviceptr dptr)
{
    CUresult result;
    static struct address_node query;
    if (!sys_cuMemFree) {
        gpu_mem_hook_init();
    }

    if (mpi_node_rank >= 0) {
      query.address_key = (void *) dptr;
      avl_remove(&address_lookup_root, avl_search(&address_lookup_root, (struct avl_node *)&query, cmp_func));
    }
    result = sys_cuMemFree(dptr);

    return (result);
}

cudaError_t CUDARTAPI cudaFree(void *dptr)
{
    cudaError_t result;
    static struct address_node query;
    if (!sys_cudaFree) {
        gpu_mem_hook_init();
    }

    if (mpi_node_rank >= 0) {
      query.address_key = (void *) dptr;
      avl_remove(&address_lookup_root, avl_search(&address_lookup_root, (struct avl_node *)&query, cmp_func));
    }
    result = sys_cudaFree(dptr);

    return result;
}

int ext_mpi_init_gpu_blocking(MPI_Comm comm_world) {
  MPI_Comm gpu_comm_node;
  int tasks_per_node = ext_mpi_get_num_tasks_per_socket(comm_world, 1);
  ext_mpi_call_mpi(PMPI_Comm_size(comm_world, &mpi_node_size));
  ext_mpi_call_mpi(PMPI_Comm_rank(comm_world, &mpi_node_rank));
  ext_mpi_call_mpi(PMPI_Comm_split(comm_world, mpi_node_rank / tasks_per_node,
                  mpi_node_rank % tasks_per_node, &gpu_comm_node));
  ext_mpi_call_mpi(PMPI_Comm_rank(gpu_comm_node, &mpi_node_rank));
  ext_mpi_call_mpi(PMPI_Comm_size(gpu_comm_node, &mpi_node_size));
  ext_mpi_call_mpi(PMPI_Comm_free(&gpu_comm_node));
  shmem_offsets = ext_mpi_get_shmem_offsets();
  return 0;
}

void ext_mpi_done_gpu_blocking() {
  static struct address_node *node;
  struct avl_node *cur = avl_first(&address_lookup_root);
  while(cur) {
    node = _get_entry(cur, struct address_node, avl);
    cur = avl_next(cur);
    avl_remove(&address_lookup_root, &node->avl);
    ext_mpi_dfree(shmem_root_gpu, node);
  }
  mpi_node_rank = -1;
}

int ext_mpi_sendrecvbuf_init_gpu_blocking(int my_mpi_rank, int my_cores_per_node, int num_sockets, char *sendbuf, char *recvbuf, size_t size, int *mem_partners_send, int *mem_partners_recv, char ***shmem, int **shmem_node, int *counter, int *ranks_node, char **sendbufs, char **recvbufs) {
  static struct address_node *node;
  CUdeviceptr pbase;
  size_t psize;
  int bc, error, i;
  long int offset;
  char **address_values;
  my_mpi_rank = my_mpi_rank % my_cores_per_node;
  if (sendbuf != recvbuf) {
    cuMemGetAddressRange(&pbase, &psize, (CUdeviceptr)sendbuf);
    sendbuf = (char*)pbase;
    size = psize;
    node = (struct address_node *)ext_mpi_dmalloc(shmem_root_gpu, sizeof(struct address_node) + mpi_node_size * sizeof(char*));
    node->address_key = sendbuf;
    node->size = size;
    assert(cudaIpcGetMemHandle(&node->cuda_mem_handle, (void *)node->address_key) == 0);
    node->address_values = (char**)((char*)node + sizeof(struct address_node));
    shmem[0][0] = (char*)avl_insert(&address_lookup_root, &node->avl, cmp_func);
  }
  cuMemGetAddressRange(&pbase, &psize, (CUdeviceptr)recvbuf);
  recvbuf = (char*)pbase;
  size = psize;
  node = (struct address_node *)ext_mpi_dmalloc(shmem_root_gpu, sizeof(struct address_node) + mpi_node_size * sizeof(char*));
  node->address_key = recvbuf;
  node->size = size;
  assert(cudaIpcGetMemHandle(&node->cuda_mem_handle, (void *)node->address_key) == 0);
  node->address_values = (char**)((char*)node + sizeof(struct address_node));
  shmem[0][1] = (char*)avl_insert(&address_lookup_root, &node->avl, cmp_func);
  sendbufs[0] = sendbuf;
  recvbufs[0] = recvbuf;
  memory_fence_store();
  for (i = 1; i < my_cores_per_node; i <<= 1) {
    bc = shmem_node[0][0] = ++(*counter);
    while ((unsigned int)(*((volatile int*)(shmem_node[i])) - bc) > INT_MAX);
  }
  memory_fence_load();
  if (sendbuf != recvbuf) {
    for (i = 1; i < my_cores_per_node; i++) {
      offset = shmem_offsets[ranks_node[(my_cores_per_node + i + my_mpi_rank) % my_cores_per_node]];
      node = (struct address_node*)(shmem[i][0] + offset);
      address_values = (char**)((char*)node->address_values + offset);
      if (!address_values[mpi_node_rank]) {
        error = cudaIpcOpenMemHandle((void **)&(address_values[mpi_node_rank]), node->cuda_mem_handle, cudaIpcMemLazyEnablePeerAccess);
	if (error) {
          printf("%s\n", cudaGetErrorString(error));
          assert(0);
        }
      }
      sendbufs[i] = address_values[mpi_node_rank];
    }
  }
  if (mem_partners_recv && mem_partners_recv[0] >= 0) {
  for (i = 1; i < my_cores_per_node; i++) {
    offset = shmem_offsets[ranks_node[(my_cores_per_node + i + my_mpi_rank) % my_cores_per_node]];
    node = (struct address_node*)(shmem[i][1] + offset);
    address_values = (char**)((char*)node->address_values + offset);
    if (!address_values[mpi_node_rank]) {
      error = cudaIpcOpenMemHandle((void **)&(address_values[mpi_node_rank]), node->cuda_mem_handle, cudaIpcMemLazyEnablePeerAccess);
      if (error) {
	printf("%s\n", cudaGetErrorString(error));
	assert(0);
      }
    }
    recvbufs[i] = address_values[mpi_node_rank];
  }
  }
  return 0;
}

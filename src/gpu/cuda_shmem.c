#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dlfcn.h>
#include <assert.h>
#include <cuda.h>
#include "gpu_shmem.h"
#include "shmem.h"
#include "ext_mpi.h"
#include "ext_mpi_native.h"
#include "ext_mpi_native_exec.h"

extern MPI_Comm ext_mpi_COMM_WORLD_dup;

int ext_mpi_gpu_sizeof_memhandle() { return (sizeof(struct cudaIpcMemHandle_st)); }

int ext_mpi_gpu_setup_shared_memory(MPI_Comm comm, int my_cores_per_node_row,
                                    int size_shared, int num_segments,
                                    int **shmemidi_gpu, char ***shmem_gpu) {
  struct cudaIpcMemHandle_st shmemid_gpu;
  MPI_Comm my_comm_node;
  int my_mpi_rank_row, my_mpi_size_row, shmemid_temp, i, j, k, flag;
  char *shmem_temp;
  ext_mpi_call_mpi(MPI_Comm_size(comm, &my_mpi_size_row));
  ext_mpi_call_mpi(MPI_Comm_rank(comm, &my_mpi_rank_row));
  ext_mpi_call_mpi(PMPI_Comm_split(comm, my_mpi_rank_row / (my_cores_per_node_row * num_segments),
                  my_mpi_rank_row % (my_cores_per_node_row * num_segments), &my_comm_node));
  ext_mpi_call_mpi(MPI_Comm_rank(my_comm_node, &my_mpi_rank_row));
  *shmem_gpu = (char **)malloc(my_cores_per_node_row * num_segments * sizeof(char *));
  *shmemidi_gpu = (int *)malloc(my_cores_per_node_row * num_segments * sizeof(int));
  if ((*shmem_gpu == NULL) || (*shmemidi_gpu == NULL)) {
    exit(14);
  }
  memset(*shmem_gpu, 0, my_cores_per_node_row * num_segments * sizeof(char *));
  memset(*shmemidi_gpu, 0, my_cores_per_node_row * num_segments * sizeof(int));
  for (i = 0; i < my_cores_per_node_row * num_segments; i++) {
    memset(&shmemid_gpu, 0, sizeof(struct cudaIpcMemHandle_st));
    if (my_mpi_rank_row == i) {
      if (!size_shared) {
	(*shmem_gpu)[i] = NULL;
	(*shmemidi_gpu)[i] = 1;
      } else {
        if (cudaMalloc((void *)&((*shmem_gpu)[i]), size_shared) != 0) {
	  printf("error cudaMalloc in file cuda_shmem.c\n");
          exit(16);
	}
        if ((*shmem_gpu)[i] == NULL)
          exit(16);
        if (cudaIpcGetMemHandle(&shmemid_gpu, (void *)((*shmem_gpu)[i])) != 0)
          exit(15);
        (*shmemidi_gpu)[i] |= 1;
      }
    }
    ext_mpi_call_mpi(PMPI_Bcast(&shmemid_gpu, sizeof(struct cudaIpcMemHandle_st), MPI_CHAR, i, my_comm_node));
    ext_mpi_call_mpi(PMPI_Barrier(my_comm_node));
    flag = 0;
    for (j = 0; j < sizeof(struct cudaIpcMemHandle_st); j++) {
      if (((char *)(&shmemid_gpu))[j]) flag = 1;
    }
    if (flag) {
      if ((*shmem_gpu)[i] == NULL) {
        if (cudaIpcOpenMemHandle((void **)&((*shmem_gpu)[i]), shmemid_gpu,
				 cudaIpcMemLazyEnablePeerAccess) != 0)
	  exit(13);
	(*shmemidi_gpu)[i] |= 2;
      }
      if ((*shmem_gpu)[i] == NULL)
	exit(2);
    } else {
      (*shmem_gpu)[i] = NULL;
      (*shmemidi_gpu)[i] |= 2;
    }
    ext_mpi_call_mpi(PMPI_Barrier(my_comm_node));
  }
  ext_mpi_call_mpi(PMPI_Comm_free(&my_comm_node));
  for (j = 0; j < (my_mpi_rank_row % (my_cores_per_node_row * num_segments)) / my_cores_per_node_row * my_cores_per_node_row; j++) {
    shmem_temp = (*shmem_gpu)[0];
    shmemid_temp = (*shmemidi_gpu)[0];
    for (i = 0; i < my_cores_per_node_row * num_segments - 1; i++) {
      (*shmem_gpu)[i] = (*shmem_gpu)[i + 1];
      (*shmemidi_gpu)[i] = (*shmemidi_gpu)[i + 1];
    }
    (*shmem_gpu)[my_cores_per_node_row * num_segments - 1] = shmem_temp;
    (*shmemidi_gpu)[my_cores_per_node_row * num_segments - 1] = shmemid_temp;
  }
  for (k = 0; k < num_segments; k++) {
    for (j = 0; j < my_mpi_rank_row % my_cores_per_node_row; j++) {
      shmem_temp = (*shmem_gpu)[my_cores_per_node_row * k];
      shmemid_temp = (*shmemidi_gpu)[my_cores_per_node_row * k];
      for (i = my_cores_per_node_row * k; i < my_cores_per_node_row * (k + 1) - 1; i++) {
        (*shmem_gpu)[i] = (*shmem_gpu)[i + 1];
        (*shmemidi_gpu)[i] = (*shmemidi_gpu)[i + 1];
      }
      (*shmem_gpu)[my_cores_per_node_row * (k + 1) - 1] = shmem_temp;
      (*shmemidi_gpu)[my_cores_per_node_row * (k + 1) - 1] = shmemid_temp;
    }
  }
  return 0;
}

int ext_mpi_gpu_destroy_shared_memory(int my_cores_per_node, int *ranks_node, int *shmemid_gpu, char **shmem_gpu) {
  MPI_Comm newcomm;
  MPI_Group world_group, group;
  int i;
  for (i = 0; i < my_cores_per_node; i++) {
    if ((shmemid_gpu[i] & 2) && shmem_gpu[i]) {
      if (cudaIpcCloseMemHandle((void *)(shmem_gpu[i])) != 0) {
        exit(13);
      }
    }
  }
  if (ranks_node) {
    ext_mpi_call_mpi(PMPI_Comm_group(ext_mpi_COMM_WORLD_dup, &world_group));
    ext_mpi_call_mpi(PMPI_Group_incl(world_group, my_cores_per_node, ranks_node, &group));
    ext_mpi_call_mpi(PMPI_Comm_create_group(ext_mpi_COMM_WORLD_dup, group, 0, &newcomm));
    ext_mpi_call_mpi(PMPI_Barrier(newcomm));
    ext_mpi_call_mpi(PMPI_Comm_free(&newcomm));
    ext_mpi_call_mpi(PMPI_Group_free(&group));
    ext_mpi_call_mpi(PMPI_Group_free(&world_group));
  }
  for (i = 0; i < my_cores_per_node; i++) {
    if ((shmemid_gpu[i] & 1) && shmem_gpu[i]) {
      if (cudaFree((void *)(shmem_gpu[i])) != 0) {
        exit(13);
      }
    }
  }
  free(shmem_gpu);
  free(shmemid_gpu);
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

struct address_lookup {
  struct address_lookup *left, *right;
  char *address_key;
  size_t size_key;
  struct cudaIpcMemHandle_st cuda_mem_handle;
  char **address_values;
};

static int mpi_node_rank = -1, mpi_node_size = -1;
static struct address_lookup *address_lookup_root;
static CUresult CUDAAPI(*sys_cuMemFree) (CUdeviceptr dptr);
static cudaError_t CUDARTAPI(*sys_cudaFree) (void *dptr);
static long int *shmem_offsets;

static int search_address_lookup(struct address_lookup *p, char *address, size_t size, char ***ret) {
  while (p && address != p->address_key) {
    if (address < p->address_key) {
      p = p->left;
    } else {
      p = p->right;
    }
  }
  if (!p) return 0;
  *ret = p->address_values;
  return 1;
}

static struct address_lookup * insert_address_lookup(struct address_lookup **p, char *address_key, size_t size_key, char **address_values) {
  while (*p && address_key != (*p)->address_key) {
    if (address_key < (*p)->address_key) {
      p = &(*p)->left;
    } else {
      p = &(*p)->right;
    }
  }
  if (!(*p)) {
    *p = (struct address_lookup*)malloc(sizeof(struct address_lookup) + mpi_node_size * sizeof(char*));
    (*p)->left = (*p)->right = NULL;
    (*p)->address_key = address_key;
    (*p)->size_key = size_key;
    assert(cudaIpcGetMemHandle(&(*p)->cuda_mem_handle, (void *)address_key) == 0);
    (*p)->address_values = (char**)((char*)p + sizeof(struct address_lookup));
    if (address_values) {
      memcpy((*p)->address_values, address_values, mpi_node_size * sizeof(char*));
    } else {
      memset((*p)->address_values, 0, mpi_node_size * sizeof(char*));
    }
  }
  return *p;
}

static void delete_all_addresses_lookup(struct address_lookup *p) {
  if (p) {
    if (p->left) delete_all_addresses_lookup(p->left);
    if (p->right) delete_all_addresses_lookup(p->right);
    free(p);
  }
}

static void merge_trees_address_lookup(struct address_lookup **root, struct address_lookup *p) {
  if (p) {
    insert_address_lookup(root, p->address_key, p->size_key, p->address_values);
    if (p->left) merge_trees_address_lookup(root, p->left);
    if (p->right) merge_trees_address_lookup(root, p->right);
  }
}

static int delete_address_lookup(struct address_lookup **p, char *address_key) {
  struct address_lookup *p2, *p3;
  while (*p && address_key != (*p)->address_key) {
    if (address_key < (*p)->address_key) {
      p = &(*p)->left;
    } else {
      p = &(*p)->right;
    }
  }
  if (!*p) return 0;
  p2 = *p;
  if (!(*p)->right) {
    *p = (*p)->left;
  } else {
    p3 = (*p)->left;
    *p = (*p)->right;
    merge_trees_address_lookup(p, p3);
    delete_all_addresses_lookup(p3);
  }
  free(p2);
  return 1;
}

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
    if (!sys_cuMemFree) {
        gpu_mem_hook_init();
    }

    if (mpi_node_rank >= 0) {
      delete_address_lookup(&address_lookup_root, (void *) dptr);
    }
    result = sys_cuMemFree(dptr);

    return (result);
}

cudaError_t CUDARTAPI cudaFree(void *dptr)
{
    cudaError_t result;
    if (!sys_cudaFree) {
        gpu_mem_hook_init();
    }

    if (mpi_node_rank >= 0) {
      delete_address_lookup(&address_lookup_root, dptr);
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
  address_lookup_root = NULL;
  return 0;
}

void ext_mpi_done_gpu_blocking() {
  int i;
  for (i = 0; i < mpi_node_size; i++) {
    delete_all_addresses_lookup(address_lookup_root);
  }
  address_lookup_root = NULL;
  mpi_node_rank = -1;
}

int ext_mpi_sendrecvbuf_init_gpu_blocking(int my_mpi_rank, int my_cores_per_node, int num_sockets, char *sendbuf, char *recvbuf, size_t size, int *mem_partners_send, int *mem_partners_recv, char ***shmem, int **shmem_node, int *counter, int *ranks_node, char **sendbufs, char **recvbufs) {
  struct address_lookup *al;
  CUdeviceptr pbase;
  size_t psize;
  int bc, error, i;
  my_mpi_rank = my_mpi_rank % my_cores_per_node;
  if (sendbuf != recvbuf) {
    cuMemGetAddressRange(&pbase, &psize, (CUdeviceptr)sendbuf);
    sendbuf = (char*)pbase;
    size = psize;
    shmem[0][0] = (char*)insert_address_lookup(&address_lookup_root, sendbuf, size, NULL);
  }
  cuMemGetAddressRange(&pbase, &psize, (CUdeviceptr)recvbuf);
  recvbuf = (char*)pbase;
  size = psize;
  shmem[0][1] = (char*)insert_address_lookup(&address_lookup_root, recvbuf, size, NULL);
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
      al = ((struct address_lookup*)(shmem[i][0]+shmem_offsets[ranks_node[(i + my_mpi_rank) % my_cores_per_node]]));
      if (!al->address_values[mpi_node_rank]) {
        assert(cudaIpcOpenMemHandle((void **)&(al->address_values[mpi_node_rank]), al->cuda_mem_handle, cudaIpcMemLazyEnablePeerAccess) == 0);
	al->address_values[mpi_node_rank] = sendbufs[i];
      }
      sendbufs[i] = al->address_values[mpi_node_rank];
    }
  }
//  if (mem_partners_recv && mem_partners_recv[0] >= 0) {
  for (i = 1; i < my_cores_per_node; i++) {
    al = ((struct address_lookup*)(shmem[i][1]+shmem_offsets[ranks_node[(i + my_mpi_rank) % my_cores_per_node]]));
    if (!al->address_values[mpi_node_rank]) {
      assert(cudaIpcOpenMemHandle((void **)&(al->address_values[mpi_node_rank]), al->cuda_mem_handle, cudaIpcMemLazyEnablePeerAccess) == 0);
    }
    recvbufs[i] = al->address_values[mpi_node_rank];
  }
//  }
  return 0;
}

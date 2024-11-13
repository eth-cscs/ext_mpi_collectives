#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dlfcn.h>
#include <cuda.h>
#include "gpu_shmem.h"
#include "shmem.h"
#include "ext_mpi.h"
#include "ext_mpi_native_exec.h"

int ext_mpi_gpu_sizeof_memhandle() { return (sizeof(struct cudaIpcMemHandle_st)); }

int ext_mpi_gpu_setup_shared_memory(MPI_Comm comm, int my_cores_per_node_row,
                                    int size_shared, int num_segments,
                                    int **shmemidi_gpu, char ***shmem_gpu) {
  struct cudaIpcMemHandle_st shmemid_gpu;
  MPI_Comm my_comm_node;
  int my_mpi_rank_row, my_mpi_size_row, shmemid_temp, i, j, k, flag;
  char *shmem_temp;
  MPI_Comm_size(comm, &my_mpi_size_row);
  MPI_Comm_rank(comm, &my_mpi_rank_row);
  PMPI_Comm_split(comm, my_mpi_rank_row / (my_cores_per_node_row * num_segments),
                  my_mpi_rank_row % (my_cores_per_node_row * num_segments), &my_comm_node);
  MPI_Comm_rank(my_comm_node, &my_mpi_rank_row);
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
        if (cudaMalloc((void *)&((*shmem_gpu)[i]), size_shared) != 0)
          exit(16);
        if ((*shmem_gpu)[i] == NULL)
          exit(16);
        if (cudaIpcGetMemHandle(&shmemid_gpu, (void *)((*shmem_gpu)[i])) != 0)
          exit(15);
        (*shmemidi_gpu)[i] |= 1;
      }
    }
    PMPI_Bcast(&shmemid_gpu, sizeof(struct cudaIpcMemHandle_st), MPI_CHAR, i, my_comm_node);
    PMPI_Barrier(my_comm_node);
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
    PMPI_Barrier(my_comm_node);
  }
  PMPI_Comm_free(&my_comm_node);
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

int ext_mpi_gpu_destroy_shared_memory(int my_cores_per_node, int *shmemid_gpu, char **shmem_gpu, char *comm_code) {
  int i;
  for (i = 0; i < my_cores_per_node; i++) {
    ext_mpi_node_barrier_mpi(MPI_COMM_NULL, MPI_COMM_NULL, comm_code);
    if ((shmemid_gpu[i] & 2) && shmem_gpu[i]) {
      if (cudaIpcCloseMemHandle((void *)(shmem_gpu[i])) != 0) {
        exit(13);
      }
    }
  }
  ext_mpi_node_barrier_mpi(MPI_COMM_NULL, MPI_COMM_NULL, comm_code);
  for (i = 0; i < my_cores_per_node; i++) {
    if ((shmemid_gpu[i] & 1) && shmem_gpu[i]) {
      if (cudaFree((void *)(shmem_gpu[i])) != 0) {
        exit(13);
      }
    }
  }
  ext_mpi_node_barrier_mpi(MPI_COMM_NULL, MPI_COMM_NULL, comm_code);
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
  PMPI_Comm_rank(comm, &my_mpi_rank);
  PMPI_Comm_size(comm, &my_mpi_size);
  PMPI_Comm_split(comm, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &gpu_comm_node);
  PMPI_Comm_rank(gpu_comm_node, &my_mpi_rank);
  PMPI_Comm_size(gpu_comm_node, &my_mpi_size);
  shmemid_gpu = (struct cudaIpcMemHandle_st*)malloc(sizeof(struct cudaIpcMemHandle_st) * my_mpi_size);
  *sendrecvbufs = (char **)malloc(my_mpi_size * sizeof(char *));
  PMPI_Allreduce(MPI_IN_PLACE, &size, 1, MPI_INT, MPI_MAX, gpu_comm_node);
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
  PMPI_Allgather(&shmemid_gpu_l, sizeof(struct cudaIpcMemHandle_st), MPI_CHAR, shmemid_gpu, sizeof(struct cudaIpcMemHandle_st), MPI_CHAR, gpu_comm_node);
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
  if (PMPI_Comm_free(&gpu_comm_node) != MPI_SUCCESS) {
    printf("error PMPI_Comm_free in cuda_shmem.c\n");
    exit(1);
  }
  return 0;
}

int ext_mpi_sendrecvbuf_done_gpu(MPI_Comm comm, int my_cores_per_node, char **sendrecvbufs) {
  MPI_Comm gpu_comm_node;
  int my_mpi_rank, my_mpi_size, i;
  char *addr;
  if (comm == MPI_COMM_NULL) {
    return -1;
  }
  PMPI_Comm_rank(comm, &my_mpi_rank);
  PMPI_Comm_size(comm, &my_mpi_size);
  PMPI_Comm_split(comm, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &gpu_comm_node);
  PMPI_Comm_rank(gpu_comm_node, &my_mpi_rank);
  PMPI_Comm_size(gpu_comm_node, &my_mpi_size);
  for (i = 1; i < my_mpi_size; i++) {
    addr = sendrecvbufs[i];
    if (addr) {
      if (cudaIpcCloseMemHandle((void *)(addr)) != 0) {
        printf("error 1 cudaIpcCloseMemHandle in cuda_shmem.c\n");
        exit(1);
      }
    }
  }
  free(sendrecvbufs);
  if (PMPI_Comm_free(&gpu_comm_node) != MPI_SUCCESS) {
    printf("error PMPI_Comm_free in cuda_shmem.c\n");
    exit(1);
  }
  return 0;
}

static int mpi_node_rank, mpi_node_size;
static struct address_lookup **address_lookup_root;
static CUresult CUDAAPI(*sys_cuMemFree) (CUdeviceptr dptr);
static cudaError_t CUDARTAPI(*sys_cudaFree) (void *dptr);

static int search_address_lookup(struct address_lookup *p, char *address, size_t size, char **ret) {
  while (p && address != p->address_key) {
    if (address < p->address_key) {
      p = p->left;
    } else {
      p = p->right;
    }
  }
  if (!p) return 0;
  *ret = p->address_value;
  return 1;
}

static int insert_address_lookup(struct address_lookup **root, char *address_key, size_t size_key, char *address_value) {
  struct address_lookup **p;
  p = root;
  while (*p && address_key != (*p)->address_key) {
    if (address_key < (*p)->address_key) {
      p = &(*p)->left;
    } else {
      p = &(*p)->right;
    }
  }
  if (*p) {
    (*p)->address_key = address_key;
    (*p)->size_key = size_key;
    (*p)->address_value = address_value;
    return 1;
  } else {
    *p = (struct address_lookup*)malloc(sizeof(struct address_lookup));
    (*p)->left = (*p)->right = NULL;
    (*p)->address_key = address_key;
    (*p)->size_key = size_key;
    (*p)->address_value = address_value;
    return 2;
  }
}

static void delete_all_addresses_lookup(struct address_lookup *p) {
  if (p) {
    if (p->left) delete_all_addresses_lookup(p->left);
    if (p->right) delete_all_addresses_lookup(p->right);
    if (p->address_value) {
      if (cudaIpcCloseMemHandle((void *)(p->address_value)) != 0) {
        printf("error 3 cudaIpcCloseMemHandle in cuda_shmem.c %p\n", p->address_value);
        exit(1);
      }
    }
    free(p);
  }
}

static void merge_trees_address_lookup(struct address_lookup **root, struct address_lookup *p) {
  if (p) {
    insert_address_lookup(root, p->address_key, p->size_key, p->address_value);
    if (p->left) merge_trees_address_lookup(root, p->left);
    if (p->right) merge_trees_address_lookup(root, p->right);
  }
}

static int delete_address_lookup(struct address_lookup **root, char *address_key) {
  struct address_lookup **p, *p2, *p3;
  p = root;
  while (*p && address_key != (*p)->address_key) {
    if (address_key < (*p)->address_key) {
      p = &(*p)->left;
    } else {
      p = &(*p)->right;
    }
  }
  if (!*p) return 0;
  if (!(*p)->right) {
    p2 = *p;
    if (p == root) {
      *p = (*p)->left;
    } else {
      p = &(*p)->left;
    }
    free(p2);
  } else {
    p2 = *p;
    p3 = (*p)->left;
    if (p == root) {
      *p = (*p)->right;
    } else {
      p = &(*p)->right;
    }
    merge_trees_address_lookup(p, p3);
    delete_all_addresses_lookup(p3);
    if (p2->address_value) {
      if (cudaIpcCloseMemHandle((void *)(p2->address_value)) != 0) {
        printf("error 4 cudaIpcCloseMemHandle in cuda_shmem.c %p\n", p2->address_value);
        exit(1);
      }
    }
    free(p2);
  }
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

    delete_address_lookup(&address_lookup_root[mpi_node_rank], (void *) dptr);
    result = sys_cuMemFree(dptr);

    return (result);
}

cudaError_t CUDARTAPI cudaFree(void *dptr)
{
    cudaError_t result;
    if (!sys_cudaFree) {
        gpu_mem_hook_init();
    }

    delete_address_lookup(&address_lookup_root[mpi_node_rank], dptr);
    result = sys_cudaFree(dptr);

    return result;
}

int ext_mpi_init_gpu_blocking(MPI_Comm comm_world) {
  MPI_Comm gpu_comm_node;
  int tasks_per_node = ext_mpi_get_num_tasks_per_socket(comm_world, 1);;
  PMPI_Comm_size(comm_world, &mpi_node_size);
  PMPI_Comm_rank(comm_world, &mpi_node_rank);
  PMPI_Comm_split(comm_world, mpi_node_rank / tasks_per_node,
                  mpi_node_rank % tasks_per_node, &gpu_comm_node);
  PMPI_Comm_rank(gpu_comm_node, &mpi_node_rank);
  PMPI_Comm_size(gpu_comm_node, &mpi_node_size);
  address_lookup_root = (struct address_lookup **)malloc(mpi_node_size * sizeof(struct address_lookup *));
  memset(address_lookup_root, 0, mpi_node_size * sizeof(struct address_lookup *));
  return 0;
}

void ext_mpi_done_gpu_blocking() {
  int i;
  for (i = 0; i < mpi_node_size; i++) {
    delete_all_addresses_lookup(address_lookup_root[i]);
  }
  free(address_lookup_root);
}

int ext_mpi_sendrecvbuf_init_gpu_blocking(int my_mpi_rank, int my_cores_per_node, int num_sockets, char *sendbuf, char *recvbuf, size_t size, int *mem_partners_send, int *mem_partners_recv, char ***shmem, int **shmem_node, int *counter, char **sendbufs, char **recvbufs) {
  CUdeviceptr pbase;
  size_t psize;
  int bc, i;
  my_mpi_rank = my_mpi_rank % my_cores_per_node;
  if (sendbuf != recvbuf) {
    cuMemGetAddressRange(&pbase, &psize, (CUdeviceptr)sendbuf);
    sendbuf = (char*)pbase;
    size = psize;
    ((struct address_transfer*)(shmem[0]))[0].address = sendbuf;
    ((struct address_transfer*)(shmem[0]))[0].size = size;
    if (!(((struct address_transfer*)(shmem[0]))[0].present = !insert_address_lookup(&address_lookup_root[mpi_node_rank], sendbuf, size, NULL))) {
      if (cudaIpcGetMemHandle(&(((struct address_transfer*)(shmem[0]))[0].cuda_mem_handle), (void *)sendbuf) != 0) {
        printf("error 2 cudaIpcGetMemHandle in cuda_shmem.c\n");
        exit(1);
      }
    }
  }
  ((struct address_transfer*)(shmem[0]))[0].mpi_node_rank = mpi_node_rank;
  cuMemGetAddressRange(&pbase, &psize, (CUdeviceptr)recvbuf);
  recvbuf = (char*)pbase;
  size = psize;
  ((struct address_transfer*)(shmem[0]))[1].address = recvbuf;
  ((struct address_transfer*)(shmem[0]))[1].size = size;
  if (!(((struct address_transfer*)(shmem[0]))[1].present = !insert_address_lookup(&address_lookup_root[mpi_node_rank], recvbuf, size, NULL))) {
    if (cudaIpcGetMemHandle(&(((struct address_transfer*)(shmem[0]))[1].cuda_mem_handle), (void *)recvbuf) != 0) {
      printf("error 3 cudaIpcGetMemHandle in cuda_shmem.c\n");
      exit(1);
    }
  }
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
      if (!((struct address_transfer*)(shmem[i]))[0].present && !search_address_lookup(address_lookup_root[((struct address_transfer*)(shmem[i]))[0].mpi_node_rank], ((struct address_transfer*)(shmem[i]))[0].address, ((struct address_transfer*)(shmem[i]))[0].size, &sendbufs[i])) {
        if (cudaIpcOpenMemHandle((void **)&(sendbufs[i]), ((struct address_transfer*)(shmem[i]))[0].cuda_mem_handle, cudaIpcMemLazyEnablePeerAccess) != 0) {
          printf("error 2 cudaIpcOpenMemHandle in cuda_shmem.c\n");
          exit(1);
        }
        insert_address_lookup(&address_lookup_root[((struct address_transfer*)(shmem[i]))[0].mpi_node_rank], ((struct address_transfer*)(shmem[i]))[0].address, ((struct address_transfer*)(shmem[i]))[0].size, sendbufs[i]);
      }
    }
  }
  for (i = 1; i < my_cores_per_node; i++) {
    if (!((struct address_transfer*)(shmem[i]))[1].present && !search_address_lookup(address_lookup_root[((struct address_transfer*)(shmem[i]))[0].mpi_node_rank], ((struct address_transfer*)(shmem[i]))[1].address, ((struct address_transfer*)(shmem[i]))[1].size, &recvbufs[i])) {
      if (cudaIpcOpenMemHandle((void **)&(recvbufs[i]), ((struct address_transfer*)(shmem[i]))[1].cuda_mem_handle, cudaIpcMemLazyEnablePeerAccess) != 0) {
        printf("error 3 cudaIpcOpenMemHandle in cuda_shmem.c\n");
        exit(1);
      }
      insert_address_lookup(&address_lookup_root[((struct address_transfer*)(shmem[i]))[0].mpi_node_rank], ((struct address_transfer*)(shmem[i]))[1].address, ((struct address_transfer*)(shmem[i]))[1].size, recvbufs[i]);
    }
  }
  return 0;
}

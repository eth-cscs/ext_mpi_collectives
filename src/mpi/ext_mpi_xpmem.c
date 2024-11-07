#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef XPMEM
#include <xpmem.h>
#endif
#include "constants.h"
#include "ext_mpi.h"
#include "ext_mpi_native_exec.h"
#include "ext_mpi_xpmem.h"
#include <mpi.h>

#ifdef XPMEM

#define PAGESIZE 4096

extern MPI_Comm ext_mpi_COMM_WORLD_dup;

static long long int *all_xpmem_id;
static long long int *all_xpmem_id_permutated;

struct xpmem_tree {
  struct xpmem_tree *left, *right;
  unsigned long int offset;
  size_t size;
  char *a;
};

static struct xpmem_tree **xpmem_tree_root;

int ext_mpi_init_xpmem(MPI_Comm comm) {
  MPI_Comm xpmem_comm_node;
  int my_mpi_rank, my_mpi_size, my_cores_per_node, i, j, k;
  long long int global_xpmem_id, *all_global_xpmem_id, a;
  my_cores_per_node = ext_mpi_get_num_tasks_per_socket(comm, 1);
  all_global_xpmem_id = (long long int *)malloc(my_cores_per_node * sizeof(long long int));
  all_xpmem_id = (long long int *)malloc(my_cores_per_node * sizeof(long long int));
  all_xpmem_id_permutated = (long long int *)malloc(my_cores_per_node * sizeof(long long int));
  PMPI_Comm_size(comm, &my_mpi_size);
  PMPI_Comm_rank(comm, &my_mpi_rank);
  PMPI_Comm_split(comm, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &xpmem_comm_node);
  PMPI_Comm_size(xpmem_comm_node, &my_mpi_size);
  PMPI_Comm_rank(xpmem_comm_node, &my_mpi_rank);
  global_xpmem_id = xpmem_make (NULL, XPMEM_MAXADDR_SIZE, XPMEM_PERMIT_MODE, (void *)0600);
  if (global_xpmem_id == -1) {
    printf("error xpmem_make\n");
    exit(1);
  }
  PMPI_Allgather(&global_xpmem_id, 1, MPI_LONG_LONG, all_global_xpmem_id, 1, MPI_LONG_LONG, xpmem_comm_node);
  for (i = 0; i < my_cores_per_node; i++) {
    if (my_mpi_rank != i) {
      all_xpmem_id[i] = xpmem_get (all_global_xpmem_id[i], XPMEM_RDWR, XPMEM_PERMIT_MODE, NULL);
      if (all_xpmem_id[i] == -1) {
        printf("error xpmem_get\n");
        exit(1);
      }
    } else {
      all_xpmem_id[i] = -1;
    }
  }
  if (PMPI_Comm_free(&xpmem_comm_node) != MPI_SUCCESS) {
    printf("error in PMPI_Comm_free in ext_mpi_xpmem.c\n");
    exit(1);
  }
  free(all_global_xpmem_id);
  for (i = 0; i < my_mpi_size; i++) {
    all_xpmem_id_permutated[i] = all_xpmem_id[i];
  }
  for (j = 0; j < my_mpi_rank / (my_mpi_size / my_cores_per_node) * (my_mpi_size / my_cores_per_node); j++) {
    a = all_xpmem_id_permutated[0];
    for (i = 0; i < my_mpi_size - 1; i++) {
      all_xpmem_id_permutated[i] = all_xpmem_id_permutated[i + 1];
    }
    all_xpmem_id_permutated[my_mpi_size - 1] = a;
  }
  for (k = 0; k < my_cores_per_node; k++) {
    for (j = 0; j < my_mpi_rank % (my_mpi_size / my_cores_per_node); j++) {
      a = all_xpmem_id_permutated[(my_mpi_size / my_cores_per_node) * k];
      for (i = (my_mpi_size / my_cores_per_node) * k; i < (my_mpi_size / my_cores_per_node) * (k + 1) - 1; i++) {
        all_xpmem_id_permutated[i] = all_xpmem_id_permutated[i + 1];
      }
      all_xpmem_id_permutated[(my_mpi_size / my_cores_per_node) * (k + 1) - 1] = a;
    }
  }
  xpmem_tree_root = (struct xpmem_tree **)malloc(my_cores_per_node * sizeof(struct xpmem_tree *));
  memset(xpmem_tree_root, 0, my_cores_per_node * sizeof(struct xpmem_tree *));
  return 0;
}

int ext_mpi_sendrecvbuf_init_xpmem(MPI_Comm comm, int my_cores_per_node, int num_sockets, char *sendrecvbuf, int size, char ***sendrecvbufs) {
  MPI_Comm xpmem_comm_node;
  struct xpmem_addr addr;
  int my_mpi_rank, my_mpi_size, i, j, k;
  char *a;
  if (!sendrecvbuf) {
    *sendrecvbufs = NULL;
    return -1;
  }
  PMPI_Comm_rank(comm, &my_mpi_rank);
  PMPI_Comm_size(comm, &my_mpi_size);
  PMPI_Comm_split(comm, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &xpmem_comm_node);
  PMPI_Comm_rank(xpmem_comm_node, &my_mpi_rank);
  PMPI_Comm_size(xpmem_comm_node, &my_mpi_size);
  *sendrecvbufs = (char **)malloc(my_mpi_size * sizeof(char *));
  PMPI_Allgather(&sendrecvbuf, 1, MPI_LONG, *sendrecvbufs, 1, MPI_LONG, xpmem_comm_node);
  PMPI_Allreduce(MPI_IN_PLACE, &size, 1, MPI_INT, MPI_MAX, xpmem_comm_node);
  size += PAGESIZE;
  while (size & (PAGESIZE - 1)) {
    size++;
  }
  for (i = 0; i < my_mpi_size; i++) {
    if (all_xpmem_id[i] != -1) {
      addr.offset = (long int)((*sendrecvbufs)[i]);
      while (addr.offset & (PAGESIZE - 1)) {
        addr.offset--;
      }
      addr.apid = all_xpmem_id[i];
      if (addr.apid != -1 && (*sendrecvbufs)[i]) {
        a = xpmem_attach(addr, size, NULL);
        if ((long int)a == -1) {
          printf("error xpmem_attach\n");
          exit(1);
        }
        (*sendrecvbufs)[i] = a + (long int)((*sendrecvbufs)[i] - addr.offset);
      } else {
	(*sendrecvbufs)[i] = sendrecvbuf;
      }
    }
  }
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
  if (PMPI_Comm_free(&xpmem_comm_node) != MPI_SUCCESS) {
    printf("error in PMPI_Comm_free in ext_mpi_xpmem.c\n");
    exit(1);
  }
  return 0;
}

int ext_mpi_sendrecvbuf_done_xpmem(MPI_Comm comm, int my_cores_per_node, char **sendrecvbufs) {
  MPI_Comm xpmem_comm_node;
  int my_mpi_rank, my_mpi_size, i;
  char *addr;
  if (comm == MPI_COMM_NULL) {
    return -1;
  }
  PMPI_Comm_rank(comm, &my_mpi_rank);
  PMPI_Comm_size(comm, &my_mpi_size);
  PMPI_Comm_split(comm, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &xpmem_comm_node);
  PMPI_Comm_rank(xpmem_comm_node, &my_mpi_rank);
  PMPI_Comm_size(xpmem_comm_node, &my_mpi_size);
  for (i = 1; i < my_mpi_size; i++) {
    addr = sendrecvbufs[i];
    while ((long int)addr & (PAGESIZE - 1)) {
      addr--;
    }
    if (xpmem_detach(addr) != 0) {
      printf("error xpmem_detach\n");
      exit(1);
    }
  }
  free(sendrecvbufs);
  if (PMPI_Comm_free(&xpmem_comm_node) != MPI_SUCCESS) {
    printf("error in PMPI_Comm_free in ext_mpi_xpmem.c\n");
    exit(1);
  }
  return 0;
}

static void xpmem_tree_put(unsigned long int offset, size_t size, int j, char *a){
  struct xpmem_tree **p;
  p = &xpmem_tree_root[j];
  while (*p && (*p)->offset != offset) {
    if (offset < (*p)->offset) {
      p = &(*p)->left;
    } else {
      p = &(*p)->right;
    }
  }
  if (*p) {
    if (size > (*p)->size) {
      (*p)->offset = offset;
      (*p)->size = size;
      (*p)->a = a;
    }
  } else {
    *p = (struct xpmem_tree *)malloc(sizeof(struct xpmem_tree));
    (*p)->left = (*p)->right = NULL;
    (*p)->offset = offset;
    (*p)->size = size;
    (*p)->a = a;
  }
}

static int xpmem_tree_get(unsigned long int offset, size_t size, int j, char **a){
  struct xpmem_tree *p;
  p = xpmem_tree_root[j];
  while (p && p->offset != offset) {
    if (offset < p->offset) {
      p = p->left;
    } else {
      p = p->right;
    }
  }
  if (p && size <= p->size) {
    *a = p->a;
    return 1;
  } else {
    return 0;
  }
}

int ext_mpi_sendrecvbuf_init_xpmem_blocking(int my_mpi_rank, int my_cores_per_node, int num_sockets, char *sendbuf, char *recvbuf, size_t size, int *mem_partners_send, int *mem_partners_recv, char ***shmem, int **shmem_node, int *counter, char **sendbufs, char **recvbufs) {
  struct xpmem_addr addr;
  int i, j;
  char *a;
  my_mpi_rank = my_mpi_rank % my_cores_per_node;
  size += 2 * PAGESIZE;
  size &= 0xFFFFFFFFFFFFFFFF - (PAGESIZE - 1);
  sendbufs[0] = shmem[0][0] = sendbuf;
  recvbufs[0] = shmem[0][1] = recvbuf;
  shmem_node[0][0] = ++(*counter);
  memory_fence_store();
  for (i = 0; (j = mem_partners_send[i]) >= 0; i++) {
    if (j > 0) {
      while ((unsigned int)(*((volatile int*)(shmem_node[j])) - *counter) > INT_MAX);
    }
  }
  memory_fence_load();
  for (i = 0; (j = mem_partners_send[i]) >= 0; i++) {
    if (j > 0) {
      sendbufs[j] = shmem[j][0];
      if (all_xpmem_id_permutated[j] != -1) {
        addr.offset = (unsigned long int)(sendbufs[j]);
	addr.offset &= 0xFFFFFFFFFFFFFFFF - (PAGESIZE - 1);
        addr.apid = all_xpmem_id_permutated[j];
        if (addr.apid != -1 && sendbufs[j]) {
	  if (!xpmem_tree_get(addr.offset, size, j, &a)) {
            a = xpmem_attach(addr, size, NULL);
            if ((long int)a == -1) {
              printf("error xpmem_attach\n");
              exit(1);
            }
            xpmem_tree_put(addr.offset, size, j, a);
	  }
          sendbufs[j] = a + (unsigned long int)(sendbufs[j] - addr.offset);
        } else {
	  sendbufs[j] = sendbuf;
	}
      }
    }
  }
  for (i = 0; (j = mem_partners_recv[i]) >= 0; i++) {
    if (j > 0) {
      recvbufs[j] = shmem[j][1];
      if (all_xpmem_id_permutated[j] != -1) {
        addr.offset = (unsigned long int)(recvbufs[j]);
	addr.offset &= 0xFFFFFFFFFFFFFFFF - (PAGESIZE - 1);
        addr.apid = all_xpmem_id_permutated[j];
        if (addr.apid != -1 && recvbufs[j]) {
	  if (!xpmem_tree_get(addr.offset, size, j, &a)) {
            a = xpmem_attach(addr, size, NULL);
            if ((long int)a == -1) {
              printf("error xpmem_attach\n");
              exit(1);
            }
            xpmem_tree_put(addr.offset, size, j, a);
	  }
          recvbufs[j] = a + (unsigned long int)(recvbufs[j] - addr.offset);
        } else {
	  recvbufs[j] = recvbuf;
        }
      }
    }
  }
  return 0;
}

int ext_mpi_sendrecvbuf_done_xpmem_blocking(char **sendbufs, char **recvbufs, int *mem_partners_send, int *mem_partners_recv) {
  int i;
  char *addr;
return 0;
  for (i = 0; mem_partners_recv[i] >= 0; i++) {
    if (mem_partners_recv[i] > 0) {
      addr = recvbufs[mem_partners_send[i]];
      addr = (char *)((unsigned long int)addr & (0xFFFFFFFFFFFFFFFF - (PAGESIZE - 1)));
      if (xpmem_detach(addr) != 0) {
        printf("error xpmem_detach\n");
        exit(1);
      }
    }
  }
  for (i = 0; mem_partners_send[i] >= 0; i++) {
    if (mem_partners_send[i] > 0) {
      addr = sendbufs[mem_partners_recv[i]];
      addr = (char *)((unsigned long int)addr & (0xFFFFFFFFFFFFFFFF - (PAGESIZE - 1)));
      if (xpmem_detach(addr) != 0) {
        printf("error xpmem_detach\n");
        exit(1);
      }
    }
  }
  return 0;
}
#endif

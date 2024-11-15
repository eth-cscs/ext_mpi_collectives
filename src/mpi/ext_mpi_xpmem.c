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

int ext_mpi_init_xpmem(MPI_Comm comm_world) {
  MPI_Comm xpmem_comm_node;
  int my_mpi_rank, my_mpi_size, my_cores_per_node, i;
  long long int global_xpmem_id, *all_global_xpmem_id;
  my_cores_per_node = ext_mpi_get_num_tasks_per_socket(comm_world, 1);
  all_global_xpmem_id = (long long int *)malloc(my_cores_per_node * sizeof(long long int));
  all_xpmem_id = (long long int *)malloc(my_cores_per_node * sizeof(long long int));
  PMPI_Comm_size(comm_world, &my_mpi_size);
  PMPI_Comm_rank(comm_world, &my_mpi_rank);
  PMPI_Comm_split(comm_world, my_mpi_rank / my_cores_per_node,
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
  return 0;
}

void ext_mpi_done_xpmem() {
  free(all_xpmem_id);
}

static void get_comm_world_ranks(MPI_Comm comm_world, MPI_Comm comm_node, int **ranks) {
  int my_cores_per_node, my_mpi_size, my_mpi_rank, mpi_size;
  MPI_Comm xpmem_comm_node;
  my_cores_per_node = ext_mpi_get_num_tasks_per_socket(comm_world, 1);
  PMPI_Comm_size(comm_world, &my_mpi_size);
  PMPI_Comm_rank(comm_world, &my_mpi_rank);
  PMPI_Comm_split(comm_world, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &xpmem_comm_node);
  PMPI_Comm_size(xpmem_comm_node, &my_mpi_size);
  PMPI_Comm_rank(xpmem_comm_node, &my_mpi_rank);
  PMPI_Comm_size(comm_node, &mpi_size);
  *ranks = (int*)malloc(mpi_size * sizeof(int));
  PMPI_Allgather(&my_mpi_rank, 1, MPI_INT, *ranks, 1, MPI_INT, comm_node);
  if (PMPI_Comm_free(&xpmem_comm_node) != MPI_SUCCESS) {
    printf("error in PMPI_Comm_free in ext_mpi_xpmem.c\n");
    exit(1);
  }
}

int ext_mpi_sendrecvbuf_init_xpmem(MPI_Comm comm_world, MPI_Comm comm, int my_cores_per_node, int num_sockets, char *sendrecvbuf, int size, char ***sendrecvbufs) {
  MPI_Comm xpmem_comm_node;
  struct xpmem_addr addr;
  int my_mpi_rank, my_mpi_size, *global_ranks, i, j, k;
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
  get_comm_world_ranks(comm_world, xpmem_comm_node, &global_ranks);
  size += PAGESIZE;
  while (size & (PAGESIZE - 1)) {
    size++;
  }
  for (i = 0; i < my_mpi_size; i++) {
    if (all_xpmem_id[global_ranks[i]] != -1) {
      addr.offset = (long int)((*sendrecvbufs)[i]);
      while (addr.offset & (PAGESIZE - 1)) {
        addr.offset--;
      }
      addr.apid = all_xpmem_id[global_ranks[i]];
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
  free(global_ranks);
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

int ext_mpi_init_xpmem_blocking(MPI_Comm comm_world, MPI_Comm comm, int num_sockets_per_node, long long int **all_xpmem_id_permutated, struct xpmem_tree ***xpmem_tree_root) {
  MPI_Comm xpmem_comm_node;
  int my_mpi_rank, my_mpi_size, my_cores_per_node, *global_ranks, i, j, k;
  long long int a;
  my_cores_per_node = ext_mpi_get_num_tasks_per_socket(comm, 1);
  (*all_xpmem_id_permutated) = (long long int *)malloc(my_cores_per_node * sizeof(long long int));
  PMPI_Comm_size(comm, &my_mpi_size);
  PMPI_Comm_rank(comm, &my_mpi_rank);
  PMPI_Comm_split(comm, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &xpmem_comm_node);
  PMPI_Comm_size(xpmem_comm_node, &my_mpi_size);
  PMPI_Comm_rank(xpmem_comm_node, &my_mpi_rank);
  get_comm_world_ranks(comm_world, xpmem_comm_node, &global_ranks);
  if (PMPI_Comm_free(&xpmem_comm_node) != MPI_SUCCESS) {
    printf("error in PMPI_Comm_free in ext_mpi_xpmem.c\n");
    exit(1);
  }
  for (i = 0; i < my_mpi_size; i++) {
    (*all_xpmem_id_permutated)[i] = all_xpmem_id[global_ranks[i]];
  }
  for (j = 0; j < my_mpi_rank / (my_mpi_size / num_sockets_per_node) * (my_mpi_size / num_sockets_per_node); j++) {
    a = (*all_xpmem_id_permutated)[0];
    for (i = 0; i < my_mpi_size - 1; i++) {
      (*all_xpmem_id_permutated)[i] = (*all_xpmem_id_permutated)[i + 1];
    }
    (*all_xpmem_id_permutated)[my_mpi_size - 1] = a;
  }
  for (k = 0; k < num_sockets_per_node; k++) {
    for (j = 0; j < my_mpi_rank % (my_mpi_size / num_sockets_per_node); j++) {
      a = (*all_xpmem_id_permutated)[(my_mpi_size / num_sockets_per_node) * k];
      for (i = (my_mpi_size / num_sockets_per_node) * k; i < (my_mpi_size / num_sockets_per_node) * (k + 1) - 1; i++) {
        (*all_xpmem_id_permutated)[i] = (*all_xpmem_id_permutated)[i + 1];
      }
      (*all_xpmem_id_permutated)[(my_mpi_size / num_sockets_per_node) * (k + 1) - 1] = a;
    }
  }
  (*xpmem_tree_root) = (struct xpmem_tree **)malloc(my_mpi_size * sizeof(struct xpmem_tree *));
  memset((*xpmem_tree_root), 0, my_mpi_size * sizeof(struct xpmem_tree *));
  free(global_ranks);
  return 0;
}

static void xpmem_tree_delete(struct xpmem_tree *p) {
  char *addr;
  if (p) {
    addr = (char*)((unsigned long int)p->a & (0xFFFFFFFFFFFFFFFF - (PAGESIZE - 1)));
    if (xpmem_detach(addr) != 0) {
      printf("error xpmem_detach\n");
      exit(1);
    }
    if (p->left) xpmem_tree_delete(p->left);
    if (p->right) xpmem_tree_delete(p->right);
    if (p->next) xpmem_tree_delete(p->next);
  }
}

void ext_mpi_done_xpmem_blocking(MPI_Comm comm, struct xpmem_tree **xpmem_tree_root) {
  MPI_Comm xpmem_comm_node;
  int my_mpi_rank, my_mpi_size, my_cores_per_node, i;
  my_cores_per_node = ext_mpi_get_num_tasks_per_socket(comm, 1);
  PMPI_Comm_size(comm, &my_mpi_size);
  PMPI_Comm_rank(comm, &my_mpi_rank);
  PMPI_Comm_split(comm, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &xpmem_comm_node);
  PMPI_Comm_size(xpmem_comm_node, &my_mpi_size);
  PMPI_Comm_rank(xpmem_comm_node, &my_mpi_rank);
  if (PMPI_Comm_free(&xpmem_comm_node) != MPI_SUCCESS) {
    printf("error in PMPI_Comm_free in ext_mpi_xpmem.c\n");
    exit(1);
  }
  for (i = 0; i < my_mpi_size; i++) {
    xpmem_tree_delete(xpmem_tree_root[i]);
  }
  free(xpmem_tree_root);
}

static void xpmem_tree_put(struct xpmem_tree **xpmem_tree_root, unsigned long int offset, size_t size, int j, char *a){
  struct xpmem_tree **p, *p2;
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
      p2 = *p;
      *p = (struct xpmem_tree *)malloc(sizeof(struct xpmem_tree));
      (*p)->left = p2->left;
      (*p)->right = p2->right;
      p2->left = p2->right = NULL;
      (*p)->next = p2;
      (*p)->offset = offset;
      (*p)->size = size;
      (*p)->a = a;
    }
  } else {
    *p = (struct xpmem_tree *)malloc(sizeof(struct xpmem_tree));
    (*p)->left = (*p)->right = (*p)->next = NULL;
    (*p)->offset = offset;
    (*p)->size = size;
    (*p)->a = a;
  }
}

static int xpmem_tree_get(struct xpmem_tree **xpmem_tree_root, unsigned long int offset, size_t size, int j, char **a){
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

int ext_mpi_sendrecvbuf_init_xpmem_blocking(struct xpmem_tree **xpmem_tree_root, int my_mpi_rank, int my_cores_per_node, int num_sockets, char *sendbuf, char *recvbuf, size_t size, long long int *all_xpmem_id_permutated, int *mem_partners_send, int *mem_partners_recv, char ***shmem, int **shmem_node, int *counter, char **sendbufs, char **recvbufs) {
  struct xpmem_addr addr;
  int bc, i, j;
  char *a;
  my_mpi_rank = my_mpi_rank % my_cores_per_node;
  size += 2 * PAGESIZE;
  size &= 0xFFFFFFFFFFFFFFFF - (PAGESIZE - 1);
  sendbufs[0] = shmem[0][0] = sendbuf;
  recvbufs[0] = shmem[0][1] = recvbuf;
  memory_fence_store();
  for (i = 1; i < my_cores_per_node; i <<= 1) {
    bc = shmem_node[0][0] = ++(*counter);
    while ((unsigned int)(*((volatile int*)(shmem_node[i])) - bc) > INT_MAX);
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
	  if (!xpmem_tree_get(xpmem_tree_root, addr.offset, size, j, &a)) {
            a = xpmem_attach(addr, size, NULL);
            if ((long int)a == -1) {
              printf("error xpmem_attach 1\n");
              exit(1);
            }
            xpmem_tree_put(xpmem_tree_root, addr.offset, size, j, a);
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
	  if (!xpmem_tree_get(xpmem_tree_root, addr.offset, size, j, &a)) {
            a = xpmem_attach(addr, size, NULL);
            if ((long int)a == -1) {
              printf("error xpmem_attach 2\n");
              exit(1);
            }
            xpmem_tree_put(xpmem_tree_root, addr.offset, size, j, a);
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
#endif

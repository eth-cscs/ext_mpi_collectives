#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef XPMEM
#include <xpmem.h>
#endif
#include "constants.h"
#include "ext_mpi.h"
#include "ext_mpi_xpmem.h"
#include <mpi.h>

#ifdef XPMEM
extern MPI_Comm ext_mpi_COMM_WORLD_dup;

long int *ext_mpi_all_xpmem_id;

int ext_mpi_init_xpmem(MPI_Comm comm) {
  MPI_Comm xpmem_comm_node;
  int my_mpi_rank, my_mpi_size, my_cores_per_node, i;
  long int global_xpmem_id, *all_global_xpmem_id;
  my_cores_per_node = ext_mpi_get_num_tasks_per_socket(comm, 1);
  all_global_xpmem_id = (long int *)malloc(my_cores_per_node * sizeof(long int));
  ext_mpi_all_xpmem_id = (long int *)malloc(my_cores_per_node * sizeof(long int));
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
  PMPI_Allgather(&global_xpmem_id, 1, MPI_LONG_INT, all_global_xpmem_id, 1, MPI_LONG_INT, xpmem_comm_node);
  for (i = 1; i < my_cores_per_node; i++) {
    if (my_mpi_rank != i) {
      ext_mpi_all_xpmem_id[i] = xpmem_get (all_global_xpmem_id[i], XPMEM_RDWR, XPMEM_PERMIT_MODE, NULL);
      if (ext_mpi_all_xpmem_id[i] == -1) {
        printf("error xpmem_get\n");
        exit(1);
      }
    } else {
      ext_mpi_all_xpmem_id[i] = -1;
    }
  }
  PMPI_Comm_free(&xpmem_comm_node);
  free(all_global_xpmem_id);
  return 0;
}

int ext_mpi_sendrecvbuf_init_xpmem(MPI_Comm comm, int my_cores_per_node, char *sendrecvbuf, int size, char ***sendrecvbufs) {
  MPI_Comm xpmem_comm_node;
  struct xpmem_addr addr;
  int my_mpi_rank, my_mpi_size, i, j;
  char *a;
  PMPI_Comm_rank(comm, &my_mpi_rank);
  PMPI_Comm_size(comm, &my_mpi_size);
  PMPI_Comm_split(comm, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &xpmem_comm_node);
  PMPI_Comm_rank(xpmem_comm_node, &my_mpi_rank);
  PMPI_Comm_size(xpmem_comm_node, &my_mpi_size);
  *sendrecvbufs = (char **)malloc(my_mpi_size * sizeof(char **));
  PMPI_Allgather(sendrecvbuf, 1, MPI_LONG_INT, *sendrecvbufs, 1, MPI_LONG_INT, xpmem_comm_node);
  PMPI_Allreduce(MPI_IN_PLACE, &size, 1, MPI_LONG_INT, MPI_MAX, xpmem_comm_node);
  while (size & (4096 - 1)) {
    size++;
  }
  for (i = 0; i < my_mpi_size; i++) {
    if (ext_mpi_all_xpmem_id[i] != -1) {
      while ((long int)((*sendrecvbufs)[i]) & (4096 - 1)) {
        (*sendrecvbufs)[i]--;
      }
      addr.offset = (long int)((*sendrecvbufs)[i]);
      addr.apid = ext_mpi_all_xpmem_id[i];
      a = xpmem_attach(addr, size, NULL);
      if ((long int)a == -1) {
        printf("error xpmem_attach\n");
        exit(1);
      }
      (*sendrecvbufs)[i] = a + (long int)((*sendrecvbufs)[i] - addr.offset);
    }
  }
  for (i = 0; i < my_mpi_rank; i++) {
    a = (*sendrecvbufs)[0];
    for (j = 1; j < my_mpi_size; j++) {
      (*sendrecvbufs)[j - 1] = (*sendrecvbufs)[j];
    }
    (*sendrecvbufs)[my_mpi_size - 1] = a;
  }
  return 0;
}

int ext_mpi_sendrecvbuf_done_xpmem(MPI_Comm comm, int my_cores_per_node, char **sendrecvbufs) {
  MPI_Comm xpmem_comm_node;
  int my_mpi_rank, my_mpi_size, i;
  char *addr;
  PMPI_Comm_rank(comm, &my_mpi_rank);
  PMPI_Comm_size(comm, &my_mpi_size);
  PMPI_Comm_split(comm, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &xpmem_comm_node);
  PMPI_Comm_rank(xpmem_comm_node, &my_mpi_rank);
  PMPI_Comm_size(xpmem_comm_node, &my_mpi_size);
  for (i = 1; i < my_mpi_size; i++) {
    addr = sendrecvbufs[i];
    while ((long int)addr & (4096 - 1)) {
      addr--;
    }
    if (xpmem_detach(addr) != 0) {
      printf("error xpmem_detach\n");
      exit(1);
    }
  }
  free(sendrecvbufs);
  return 0;
}
#endif

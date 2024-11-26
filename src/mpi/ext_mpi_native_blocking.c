#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "prime_factors.h"
#include "alltoall.h"
#include "buffer_offset.h"
#include "byte_code.h"
#include "clean_barriers.h"
#include "constants.h"
#include "ext_mpi_native.h"
#include "waitany.h"
#include "padding_factor.h"
#include "shmem.h"
#include "ext_mpi.h"
#include "ext_mpi_native_exec.h"
#include "ext_mpi_native_blocking.h"
#include "ports_groups.h"
#include "ext_mpi_xpmem.h"
#include <mpi.h>
#ifdef GPU_ENABLED
#include "gpu_core.h"
#include "gpu_shmem.h"
#include "cuda_gemv.h"
#endif
#ifdef NCCL_ENABLED
#include <nccl.h>
ncclComm_t ext_mpi_nccl_comm;
#endif

extern MPI_Comm ext_mpi_COMM_WORLD_dup;

MPI_Comm ext_mpi_comm_array[2048] = {MPI_COMM_NULL};

int ext_mpi_my_cores_per_node_array[2048] = {-1};

static int *e_handle_code_max = NULL;
static char ***e_comm_code = NULL;
static char ***e_execution_pointer = NULL;
static int **e_active_wait = NULL;

static int *e_is_initialised = NULL;
static MPI_Comm *e_EXT_MPI_COMM_WORLD = NULL;
static int *e_tag_max = NULL;

struct shmem_blocking {
  char **small_mem;
  int *small_shmemid;
  int *small_sizes;
  char **mem;
  int *shmemid;
  int *sizes;
};

#define ALGORITHM_MAX 101

struct comm_comm_blocking {
  int mpi_size_blocking;
  int mpi_rank_blocking;
  int padding_factor_allreduce_blocking[ALGORITHM_MAX];
  int padding_factor_reduce_scatter_block_blocking[ALGORITHM_MAX];
  int count_allreduce_blocking[ALGORITHM_MAX];
  int count_reduce_scatter_block_blocking[ALGORITHM_MAX];
  int count_allgather_blocking[ALGORITHM_MAX];
  struct shmem_blocking shmem_blocking1;
  struct shmem_blocking shmem_blocking2;
  char locmem_blocking[1024];
  char *comm_code_allreduce_blocking[ALGORITHM_MAX];
  char *comm_code_reduce_scatter_block_blocking[ALGORITHM_MAX];
  char *comm_code_allgather_blocking[ALGORITHM_MAX];
  char **shmem_socket_blocking;
  int *shmem_socket_blocking_shmemid;
  int counter_socket_blocking;
  int socket_rank_blocking;
  int num_cores_blocking;
  char **shmem_node_blocking;
  int *shmem_node_blocking_shmemid;
  int counter_node_blocking;
  int num_sockets_per_node_blocking;
  void *p_dev_temp;
  int *sizes_shared_node;
  int *sizes_shared_socket;
  int *mem_partners_send[ALGORITHM_MAX];
  int *mem_partners_recv[ALGORITHM_MAX];
  long long int *all_xpmem_id_permutated;
  struct xpmem_tree **xpmem_tree_root;
  int copyin[ALGORITHM_MAX];
  int *ranks_node;
  struct comm_properties comm_property[ALGORITHM_MAX];
};

static struct comm_comm_blocking **(comms_blocking[(enum collective_subtypes)(size)]) = { NULL };

/*static int global_min(int i, MPI_Comm comm_row, MPI_Comm comm_column) {
  ext_mpi_call_mpi(PMPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row));
  if (comm_column != MPI_COMM_NULL) {
    ext_mpi_call_mpi(PMPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_column));
  }
  return (i);
}*/

static int add_blocking_member(int count, MPI_Datatype datatype, int handle, char **comm_code_blocking, int *count_blocking, int pfactor, MPI_Comm comm, int tag, int *copyina, int copyin) {
  int type_size, i = 0;
  ext_mpi_call_mpi(MPI_Type_size(datatype, &type_size));
  while (comm_code_blocking[i]) i++;
  comm_code_blocking[i] = (*e_comm_code)[handle];
  (*e_comm_code)[handle] = 0;
  (*e_execution_pointer)[handle] = NULL;
  (*e_active_wait)[handle] = 0;
  ext_mpi_normalize_blocking(comm_code_blocking[i], comm, tag, type_size * pfactor, NULL);
  count_blocking[i] = count * type_size;
  copyina[i] = copyin;
  return 0;
}

static int init_blocking_native(MPI_Comm comm, int i_comm) {
  char data_num_ports[1000], data_copyin_factors[1000], filename[1000];
  FILE *data;
  int message_size, *num_ports = NULL, *groups = NULL, copyin_method, *copyin_factors = NULL, num_sockets_per_node = 1, cores_per_node, mpi_rank, mpi_size, message_size_old = -1, on_gpu = 0, i_cprop = 0, i;
  for (i = 0; i < (enum collective_subtypes)(size); i++) {
    if (!(comms_blocking[i])) {
      comms_blocking[i] = (struct comm_comm_blocking **)malloc(1000 * sizeof(struct comm_comm_blocking *));
      memset(comms_blocking[i], 0, 1000 * sizeof(struct comm_comm_blocking *));
    }
    comms_blocking[i][i_comm] = (struct comm_comm_blocking *)malloc(sizeof(struct comm_comm_blocking));
    memset(comms_blocking[i][i_comm], 0, sizeof(struct comm_comm_blocking));
  }
  ext_mpi_call_mpi(MPI_Comm_rank(comm, &mpi_rank));
  ext_mpi_call_mpi(MPI_Comm_size(comm, &mpi_size));
  cores_per_node = ext_mpi_get_num_tasks_per_socket(comm, num_sockets_per_node);
  ext_mpi_my_cores_per_node_array[i_comm] = cores_per_node;
  sprintf(filename, "ext_mpi_blocking_%d_%d.txt", mpi_size / cores_per_node, cores_per_node);
  data = fopen(filename, "r");
  if (!data) {
    if (mpi_rank == 0) printf("file %s missing\n", filename);
    exit(1);
  }
  while (fscanf(data, "%d %d %s %s", &message_size, &num_sockets_per_node, data_num_ports, data_copyin_factors) == 4) {
    if (message_size <= message_size_old) {
      on_gpu = 1;
      i_cprop = 0;
    }
    message_size_old = message_size;
    for (i = 0; data_num_ports[i]; i++) {
      if (data_num_ports[i] == '_') data_num_ports[i] = ' ';
    }
    for (i = 0; data_copyin_factors[i]; i++) {
      if (data_copyin_factors[i] == '_') data_copyin_factors[i] = ' ';
    }
    ext_mpi_scan_ports_groups(data_num_ports, &num_ports, &groups);
    ext_mpi_scan_copyin(data_copyin_factors, &copyin_method, &copyin_factors);
    if (!on_gpu) {
      comms_blocking[in_place][i_comm]->count_allreduce_blocking[i_cprop] = message_size;
      comms_blocking[in_place][i_comm]->comm_property[i_cprop].count = message_size;
      comms_blocking[in_place][i_comm]->comm_property[i_cprop].num_sockets_per_node = num_sockets_per_node;
      comms_blocking[in_place][i_comm]->comm_property[i_cprop].copyin = copyin_method;
      comms_blocking[in_place][i_comm]->comm_property[i_cprop].num_ports = num_ports;
      comms_blocking[in_place][i_comm]->comm_property[i_cprop].groups = groups;
      comms_blocking[in_place][i_comm]->comm_property[i_cprop].copyin_factors = copyin_factors;
      comms_blocking[out_of_place][i_comm]->count_allreduce_blocking[i_cprop] = message_size;
      comms_blocking[out_of_place][i_comm]->comm_property[i_cprop].count = message_size;
      comms_blocking[out_of_place][i_comm]->comm_property[i_cprop].num_sockets_per_node = num_sockets_per_node;
      comms_blocking[out_of_place][i_comm]->comm_property[i_cprop].copyin = copyin_method;
      comms_blocking[out_of_place][i_comm]->comm_property[i_cprop].num_ports = num_ports;
      comms_blocking[out_of_place][i_comm]->comm_property[i_cprop].groups = groups;
      comms_blocking[out_of_place][i_comm]->comm_property[i_cprop].copyin_factors = copyin_factors;
#ifdef GPU_ENABLED
    } else {
      comms_blocking[in_place_gpu][i_comm]->count_allreduce_blocking[i_cprop] = message_size;
      comms_blocking[in_place_gpu][i_comm]->comm_property[i_cprop].count = message_size;
      comms_blocking[in_place_gpu][i_comm]->comm_property[i_cprop].num_sockets_per_node = num_sockets_per_node;
      comms_blocking[in_place_gpu][i_comm]->comm_property[i_cprop].copyin = copyin_method;
      comms_blocking[in_place_gpu][i_comm]->comm_property[i_cprop].num_ports = num_ports;
      comms_blocking[in_place_gpu][i_comm]->comm_property[i_cprop].groups = groups;
      comms_blocking[in_place_gpu][i_comm]->comm_property[i_cprop].copyin_factors = copyin_factors;
      comms_blocking[out_of_place_gpu][i_comm]->count_allreduce_blocking[i_cprop] = message_size;
      comms_blocking[out_of_place_gpu][i_comm]->comm_property[i_cprop].count = message_size;
      comms_blocking[out_of_place_gpu][i_comm]->comm_property[i_cprop].num_sockets_per_node = num_sockets_per_node;
      comms_blocking[out_of_place_gpu][i_comm]->comm_property[i_cprop].copyin = copyin_method;
      comms_blocking[out_of_place_gpu][i_comm]->comm_property[i_cprop].num_ports = num_ports;
      comms_blocking[out_of_place_gpu][i_comm]->comm_property[i_cprop].groups = groups;
      comms_blocking[out_of_place_gpu][i_comm]->comm_property[i_cprop].copyin_factors = copyin_factors;
#endif
    }
    i_cprop++;
  }
  fclose(data);
  return 0;
}

static int add_blocking_native(int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, int my_cores_per_node, int *num_ports, int *groups, int copyin, int *copyin_factors, int bit, int recursive, int arecursive, int blocking, int num_sockets_per_node, enum ecollective_type collective_type, int i_comm, struct comm_comm_blocking ***comms_blocking, long int send_ptr, long int recv_ptr) {
  MPI_Comm comm_node;
  int handle, size_shared = 1024*1024, *recvcounts, *displs, padding_factor, type_size, my_mpi_size, my_mpi_rank, i, j;
  char *comm_code_temp;
  struct header_byte_code *header;
  ext_mpi_call_mpi(MPI_Type_size(datatype, &type_size));
  if (1) {
#ifdef GPU_ENABLED
    if (recv_ptr == RECV_PTR_GPU) {
      ext_mpi_gpu_malloc(&(*comms_blocking)[i_comm]->p_dev_temp, 10000);
    }
#endif
    ext_mpi_call_mpi(MPI_Comm_size(comm, &(*comms_blocking)[i_comm]->mpi_size_blocking));
    ext_mpi_call_mpi(MPI_Comm_rank(comm, &(*comms_blocking)[i_comm]->mpi_rank_blocking));
    (*comms_blocking)[i_comm]->ranks_node = (int*)malloc(my_cores_per_node * sizeof(int));
    ext_mpi_call_mpi(PMPI_Comm_size(comm, &my_mpi_size));
    ext_mpi_call_mpi(PMPI_Comm_rank(comm, &my_mpi_rank));
    ext_mpi_call_mpi(PMPI_Comm_split(comm, my_mpi_rank / my_cores_per_node, my_mpi_rank % my_cores_per_node, &comm_node));
    ext_mpi_call_mpi(MPI_Comm_rank(ext_mpi_COMM_WORLD_dup, &i));
    ext_mpi_call_mpi(PMPI_Allgather(&i, 1, MPI_INT, (*comms_blocking)[i_comm]->ranks_node, 1, MPI_INT, comm_node));
    ext_mpi_call_mpi(PMPI_Comm_free(&comm_node));
#ifdef XPMEM
    ext_mpi_init_xpmem_blocking(comm, num_sockets_per_node, &(*comms_blocking)[i_comm]->all_xpmem_id_permutated, &(*comms_blocking)[i_comm]->xpmem_tree_root);
#endif
    comm_code_temp = (char *)malloc(sizeof(struct header_byte_code) + 2 * sizeof(MPI_Comm) + 2 * sizeof(void*));
    header = (struct header_byte_code *)comm_code_temp;
    header->size_to_return = -1;
    ext_mpi_setup_shared_memory(comm, my_cores_per_node, num_sockets_per_node, size_shared, &(*comms_blocking)[i_comm]->sizes_shared_socket, &(*comms_blocking)[i_comm]->shmem_socket_blocking_shmemid, &(*comms_blocking)[i_comm]->shmem_socket_blocking);
    (*comms_blocking)[i_comm]->counter_socket_blocking = 0;
    (*comms_blocking)[i_comm]->num_cores_blocking = my_cores_per_node;
    (*comms_blocking)[i_comm]->socket_rank_blocking = (*comms_blocking)[i_comm]->mpi_rank_blocking % (*comms_blocking)[i_comm]->num_cores_blocking;
    ext_mpi_setup_shared_memory(comm, my_cores_per_node, num_sockets_per_node, size_shared, &(*comms_blocking)[i_comm]->sizes_shared_node, &(*comms_blocking)[i_comm]->shmem_node_blocking_shmemid, &(*comms_blocking)[i_comm]->shmem_node_blocking);
    (*comms_blocking)[i_comm]->counter_node_blocking = 0;
    (*comms_blocking)[i_comm]->num_sockets_per_node_blocking = num_sockets_per_node;
#ifdef GPU_ENABLED
    ext_mpi_setup_shared_memory(comm, my_cores_per_node, num_sockets_per_node, 2 * sizeof(struct address_transfer), &(*comms_blocking)[i_comm]->shmem_blocking1.small_sizes, &(*comms_blocking)[i_comm]->shmem_blocking1.small_shmemid, &(*comms_blocking)[i_comm]->shmem_blocking1.small_mem);
    ext_mpi_setup_shared_memory(comm, my_cores_per_node, num_sockets_per_node, 2 * sizeof(struct address_transfer), &(*comms_blocking)[i_comm]->shmem_blocking2.small_sizes, &(*comms_blocking)[i_comm]->shmem_blocking2.small_shmemid, &(*comms_blocking)[i_comm]->shmem_blocking2.small_mem);
#else
#ifdef XPMEM
    ext_mpi_setup_shared_memory(comm, my_cores_per_node, num_sockets_per_node, 2 * sizeof(void*), &(*comms_blocking)[i_comm]->shmem_blocking1.small_sizes, &(*comms_blocking)[i_comm]->shmem_blocking1.small_shmemid, &(*comms_blocking)[i_comm]->shmem_blocking1.small_mem);
    ext_mpi_setup_shared_memory(comm, my_cores_per_node, num_sockets_per_node, 2 * sizeof(void*), &(*comms_blocking)[i_comm]->shmem_blocking2.small_sizes, &(*comms_blocking)[i_comm]->shmem_blocking2.small_shmemid, &(*comms_blocking)[i_comm]->shmem_blocking2.small_mem);
#endif
#endif
    size_shared = 100 * 1024 * 1024 / 8;
#ifdef GPU_ENABLED
    if (recv_ptr != RECV_PTR_GPU) {
#endif
      ext_mpi_setup_shared_memory(comm, my_cores_per_node, num_sockets_per_node, size_shared, &(*comms_blocking)[i_comm]->shmem_blocking1.sizes, &(*comms_blocking)[i_comm]->shmem_blocking1.shmemid, &(*comms_blocking)[i_comm]->shmem_blocking1.mem);
      ext_mpi_setup_shared_memory(comm, my_cores_per_node, num_sockets_per_node, size_shared, &(*comms_blocking)[i_comm]->shmem_blocking2.sizes, &(*comms_blocking)[i_comm]->shmem_blocking2.shmemid, &(*comms_blocking)[i_comm]->shmem_blocking2.mem);
#ifdef GPU_ENABLED
    } else {
      ext_mpi_gpu_setup_shared_memory(comm, my_cores_per_node, size_shared, 1, &(*comms_blocking)[i_comm]->shmem_blocking1.shmemid, &(*comms_blocking)[i_comm]->shmem_blocking1.mem);
      ext_mpi_gpu_setup_shared_memory(comm, my_cores_per_node, size_shared, 1, &(*comms_blocking)[i_comm]->shmem_blocking2.shmemid, &(*comms_blocking)[i_comm]->shmem_blocking2.mem);
    }
#endif
    free(comm_code_temp);
    switch (collective_type) {
      case collective_type_allreduce:
        for (i = 0; (*comms_blocking)[i_comm]->comm_code_allreduce_blocking[i]; i++)
          ;
        if ((*comms_blocking)[i_comm]->mpi_size_blocking < count) {
          (*comms_blocking)[i_comm]->padding_factor_allreduce_blocking[i] = (*comms_blocking)[i_comm]->mpi_size_blocking;
	  j = (*comms_blocking)[i_comm]->mpi_size_blocking;
        } else {
          (*comms_blocking)[i_comm]->padding_factor_allreduce_blocking[i] = count;
	  j = count;
        }
        handle = EXT_MPI_Allreduce_init_native((char *)(send_ptr), (char *)(recv_ptr), j, datatype, op, comm, my_cores_per_node, MPI_COMM_NULL, 1, num_ports, groups, copyin, copyin_factors, 0, bit, 0, 0, 0, num_sockets_per_node, 1, (*comms_blocking)[i_comm]->locmem_blocking, &padding_factor, &(*comms_blocking)[i_comm]->mem_partners_send[i], &(*comms_blocking)[i_comm]->mem_partners_recv[i]);
        padding_factor = 1;
        add_blocking_member(count, datatype, handle, (*comms_blocking)[i_comm]->comm_code_allreduce_blocking, (*comms_blocking)[i_comm]->count_allreduce_blocking, padding_factor, comm, 1, (*comms_blocking)[i_comm]->copyin, copyin);
      break;
      case collective_type_reduce_scatter_block:
        recvcounts = (int *)malloc((*comms_blocking)[i_comm]->mpi_size_blocking * sizeof(int));
        for (i = 0; i < (*comms_blocking)[i_comm]->mpi_size_blocking; i++) {
// FIXME
          recvcounts[i] = (*comms_blocking)[i_comm]->num_cores_blocking * CACHE_LINE_SIZE;
          recvcounts[i] = (*comms_blocking)[i_comm]->num_cores_blocking;
        }
        handle = EXT_MPI_Reduce_scatter_init_native((char *)(send_ptr), (char *)(recv_ptr), recvcounts, datatype, op, comm, my_cores_per_node, MPI_COMM_NULL, 1, num_ports, groups, copyin, copyin_factors, 0, arecursive, 0, num_sockets_per_node, 1, (*comms_blocking)[i_comm]->locmem_blocking, &padding_factor);
        for (i = 0; (*comms_blocking)[i_comm]->comm_code_reduce_scatter_block_blocking[i]; i++)
	  ;
        (*comms_blocking)[i_comm]->padding_factor_reduce_scatter_block_blocking[i] = padding_factor;
// FIXME
//        add_blocking_member(count, datatype, handle, (*comms_blocking)[i_comm]->comm_code_reduce_scatter_block_blocking, (*comms_blocking)[i_comm]->count_reduce_scatter_block_blocking, (*comms_blocking)[i_comm]->num_cores_blocking * CACHE_LINE_SIZE / padding_factor, MPI_COMM_NULL, 0, NULL);
//        add_blocking_member(count, datatype, handle, (*comms_blocking)[i_comm]->comm_code_reduce_scatter_block_blocking, (*comms_blocking)[i_comm]->count_reduce_scatter_block_blocking, (*comms_blocking)[i_comm]->num_cores_blocking, MPI_COMM_NULL, 0, NULL);
        free(recvcounts);
      break;
      case collective_type_allgather:
        recvcounts = (int *)malloc((*comms_blocking)[i_comm]->mpi_size_blocking * sizeof(int));
        displs = (int *)malloc((*comms_blocking)[i_comm]->mpi_size_blocking * sizeof(int));
        for (i = 0; i < (*comms_blocking)[i_comm]->mpi_size_blocking; i++) {
          recvcounts[i] = 1;
          displs[i] = i;
        }
        handle = EXT_MPI_Allgatherv_init_native((char *)(send_ptr), 1, datatype, (char *)(recv_ptr), recvcounts, displs, datatype, comm, my_cores_per_node, MPI_COMM_NULL, 1, num_ports, groups, 0, arecursive, 0, num_sockets_per_node, 1, (*comms_blocking)[i_comm]->locmem_blocking);
// FIXME
//        add_blocking_member(count, datatype, handle, (*comms_blocking)[i_comm]->comm_code_allgather_blocking, (*comms_blocking)[i_comm]->count_allgather_blocking, 1, MPI_COMM_NULL, 0, NULL);
//        add_blocking_member(count, datatype, handle, (*comms_blocking)[i_comm]->comm_code_allgather_blocking, (*comms_blocking)[i_comm]->count_allgather_blocking, (*comms_blocking)[i_comm]->num_cores_blocking, MPI_COMM_NULL, 0, NULL);
        free(displs);
        free(recvcounts);
      break;
      default:
      printf("collective_type not implemented\n");
      exit(1);
    }
  }
  return 0;
}

int EXT_MPI_Initialized_blocking_native(enum collective_subtypes collective_subtype, int i_comm) {
  return comms_blocking[collective_subtype] != NULL && comms_blocking[collective_subtype][i_comm] != NULL;
}

int EXT_MPI_Add_blocking_native(int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, int my_cores_per_node, int *num_ports, int *groups, int copyin, int *copyin_factors, int bit, int recursive, int arecursive, int blocking, int num_sockets_per_node, enum ecollective_type collective_type, enum collective_subtypes collective_subtype, int i_comm) {
  ext_mpi_native_export(&e_handle_code_max, &e_comm_code, &e_execution_pointer, &e_active_wait, &e_is_initialised, &e_EXT_MPI_COMM_WORLD, &e_tag_max);
  switch (collective_subtype) {
    case out_of_place:
      add_blocking_native(count, datatype, op, comm, my_cores_per_node, num_ports, groups, copyin, copyin_factors, bit, recursive, arecursive, blocking, num_sockets_per_node, collective_type, i_comm, &comms_blocking[collective_subtype], SEND_PTR_CPU, RECV_PTR_CPU);
      break;
    case in_place:
      add_blocking_native(count, datatype, op, comm, my_cores_per_node, num_ports, groups, copyin, copyin_factors, bit, recursive, arecursive, blocking, num_sockets_per_node, collective_type, i_comm, &comms_blocking[collective_subtype], RECV_PTR_CPU, RECV_PTR_CPU);
      break;
#ifdef GPU_ENABLED
    case out_of_place_gpu:
      add_blocking_native(count, datatype, op, comm, my_cores_per_node, num_ports, groups, copyin, copyin_factors, bit, recursive, arecursive, blocking, num_sockets_per_node, collective_type, i_comm, &comms_blocking[collective_subtype], SEND_PTR_GPU, RECV_PTR_GPU);
      break;
    case in_place_gpu:
      add_blocking_native(count, datatype, op, comm, my_cores_per_node, num_ports, groups, copyin, copyin_factors, bit, recursive, arecursive, blocking, num_sockets_per_node, collective_type, i_comm, &comms_blocking[collective_subtype], RECV_PTR_GPU, RECV_PTR_GPU);
      break;
#endif
    default:
      printf("error in EXT_MPI_Add_blocking_native file ext_mpi_native_blocking.c");
      exit(1);
  }
  return 0;
}

static int release_blocking_native(int i_comm, struct comm_comm_blocking ***comms_blocking, int in_place) {
  struct header_byte_code *header;
  int i;
  if ((*comms_blocking)[i_comm]) {
    if (!in_place) {
      for (i = 0; (*comms_blocking)[i_comm]->comm_property[i].count; i++) {
        free((*comms_blocking)[i_comm]->comm_property[i].num_ports);
        free((*comms_blocking)[i_comm]->comm_property[i].groups);
        free((*comms_blocking)[i_comm]->comm_property[i].copyin_factors);
      }
    }
    free((*comms_blocking)[i_comm]->all_xpmem_id_permutated);
#ifdef GPU_ENABLED
    if (!(*comms_blocking)[i_comm]->p_dev_temp) {
#endif
      ext_mpi_destroy_shared_memory((*comms_blocking)[i_comm]->num_cores_blocking, (*comms_blocking)[i_comm]->ranks_node, (*comms_blocking)[i_comm]->shmem_blocking1.sizes, (*comms_blocking)[i_comm]->shmem_blocking1.shmemid, (*comms_blocking)[i_comm]->shmem_blocking1.mem);
      ext_mpi_destroy_shared_memory((*comms_blocking)[i_comm]->num_cores_blocking, (*comms_blocking)[i_comm]->ranks_node, (*comms_blocking)[i_comm]->shmem_blocking2.sizes, (*comms_blocking)[i_comm]->shmem_blocking2.shmemid, (*comms_blocking)[i_comm]->shmem_blocking2.mem);
#ifdef GPU_ENABLED
    } else {
      if ((*comms_blocking)[i_comm]->shmem_blocking1.shmemid) {
        ext_mpi_gpu_destroy_shared_memory((*comms_blocking)[i_comm]->num_cores_blocking, (*comms_blocking)[i_comm]->ranks_node, (*comms_blocking)[i_comm]->shmem_blocking1.shmemid, (*comms_blocking)[i_comm]->shmem_blocking1.mem);
      }
      if ((*comms_blocking)[i_comm]->shmem_blocking2.shmemid) {
        ext_mpi_gpu_destroy_shared_memory((*comms_blocking)[i_comm]->num_cores_blocking, (*comms_blocking)[i_comm]->ranks_node, (*comms_blocking)[i_comm]->shmem_blocking2.shmemid, (*comms_blocking)[i_comm]->shmem_blocking2.mem);
      }
    }
#endif
#if defined XPMEM || defined GPU_ENABLED
    ext_mpi_destroy_shared_memory((*comms_blocking)[i_comm]->num_cores_blocking, (*comms_blocking)[i_comm]->ranks_node, (*comms_blocking)[i_comm]->shmem_blocking1.small_sizes, (*comms_blocking)[i_comm]->shmem_blocking1.small_shmemid, (*comms_blocking)[i_comm]->shmem_blocking1.small_mem);
    ext_mpi_destroy_shared_memory((*comms_blocking)[i_comm]->num_cores_blocking, (*comms_blocking)[i_comm]->ranks_node, (*comms_blocking)[i_comm]->shmem_blocking2.small_sizes, (*comms_blocking)[i_comm]->shmem_blocking2.small_shmemid, (*comms_blocking)[i_comm]->shmem_blocking2.small_mem);
#endif
    ext_mpi_destroy_shared_memory((*comms_blocking)[i_comm]->num_cores_blocking, (*comms_blocking)[i_comm]->ranks_node, (*comms_blocking)[i_comm]->sizes_shared_socket, (*comms_blocking)[i_comm]->shmem_socket_blocking_shmemid, (*comms_blocking)[i_comm]->shmem_socket_blocking);
    ext_mpi_destroy_shared_memory((*comms_blocking)[i_comm]->num_cores_blocking, (*comms_blocking)[i_comm]->ranks_node, (*comms_blocking)[i_comm]->sizes_shared_node, (*comms_blocking)[i_comm]->shmem_node_blocking_shmemid, (*comms_blocking)[i_comm]->shmem_node_blocking);
    for (i = 0; i < 101; i++) {
      header = (struct header_byte_code*)(*comms_blocking)[i_comm]->comm_code_allreduce_blocking[i];
      if (header) {
        free(header->barrier_shmem_node);
        free(header->barrier_shmem_socket);
        free(header->barrier_shmem_socket_small);
      }
      free((*comms_blocking)[i_comm]->comm_code_allreduce_blocking[i]);
      free((*comms_blocking)[i_comm]->comm_code_reduce_scatter_block_blocking[i]);
      free((*comms_blocking)[i_comm]->comm_code_allgather_blocking[i]);
    }
#ifdef XPMEM
    ext_mpi_done_xpmem_blocking((*comms_blocking)[i_comm]->num_cores_blocking, (*comms_blocking)[i_comm]->xpmem_tree_root);
#endif
    free((*comms_blocking)[i_comm]->ranks_node);
#ifdef GPU_ENABLED
    if ((*comms_blocking)[i_comm]->p_dev_temp) {
      ext_mpi_gpu_free((*comms_blocking)[i_comm]->p_dev_temp);
    }
#endif
    free((*comms_blocking)[i_comm]);
    (*comms_blocking)[i_comm] = NULL;
  }
  if (i_comm == 0) {
    free(*comms_blocking);
    *comms_blocking = NULL;
  }
  return 0;
}

int EXT_MPI_Release_blocking_native(int i_comm) {
  enum collective_subtypes collective_subtype;
  for (collective_subtype = 0; collective_subtype < (enum collective_subtypes)(size); collective_subtype++) {
    release_blocking_native(i_comm, &comms_blocking[collective_subtype], collective_subtype == in_place || collective_subtype == in_place_gpu);
  }
  return 0;
}

int EXT_MPI_Allreduce_native(const void *sendbuf, void *recvbuf, int count, int reduction_op, int i_comm) {
  enum collective_subtypes collective_subtype;
  struct comm_comm_blocking *comms_blocking_;
  int type_size, ccount, i = 0;
  struct shmem_blocking shmem_temp;
  void *sendbufs[0x1000], *recvbufs[0x1000];
  type_size = get_type_size(reduction_op);
  ccount = count * type_size;
#ifdef GPU_ENABLED
  if (!ext_mpi_gpu_is_device_pointer(recvbuf)) {
#endif
    if (sendbuf != recvbuf) {
      collective_subtype = out_of_place;
    } else {
      collective_subtype = in_place;
    }
#ifdef GPU_ENABLED
  } else {
    if (sendbuf != recvbuf) {
      collective_subtype = out_of_place_gpu;
    } else {
      collective_subtype = in_place_gpu;
    }
  }
#endif
  if (!comms_blocking[collective_subtype] || !comms_blocking[collective_subtype][i_comm]) {
    init_blocking_native(ext_mpi_comm_array[i_comm], i_comm);
  }
  comms_blocking_ = comms_blocking[collective_subtype][i_comm];
  while (comms_blocking_->count_allreduce_blocking[i] <= ccount && comms_blocking_->comm_code_allreduce_blocking[i + 1]) i++;
  if (comms_blocking_->count_allreduce_blocking[i] > ccount && i > 0) i--;
  if (!comms_blocking_->comm_code_allreduce_blocking[i]) {
    EXT_MPI_Add_blocking_native(comms_blocking_->comm_property[i].count / sizeof(long int), MPI_LONG, MPI_SUM, ext_mpi_comm_array[i_comm], ext_mpi_my_cores_per_node_array[i_comm], comms_blocking_->comm_property[i].num_ports, comms_blocking_->comm_property[i].groups, comms_blocking_->comm_property[i].copyin, comms_blocking_->comm_property[i].copyin_factors, 0, 1, 1, 1, comms_blocking_->comm_property[i].num_sockets_per_node, collective_type_allreduce, collective_subtype, i_comm);
  }
  if (comms_blocking_->copyin[i] < 8) {
#ifdef GPU_ENABLED
    if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
      ext_mpi_sendrecvbuf_init_gpu_blocking(comms_blocking_->mpi_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->num_sockets_per_node_blocking, (char*)sendbuf, recvbuf, ccount, comms_blocking_->mem_partners_send[i], comms_blocking_->mem_partners_recv[i], (char ***)comms_blocking_->shmem_blocking1.small_mem, (int**)comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, (char**)sendbufs, (char**)recvbufs);
    } else {
#endif
#ifdef XPMEM
      ext_mpi_sendrecvbuf_init_xpmem_blocking(comms_blocking_->xpmem_tree_root, comms_blocking_->mpi_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->num_sockets_per_node_blocking, (char*)sendbuf, recvbuf, ccount, comms_blocking_->all_xpmem_id_permutated, comms_blocking_->mem_partners_send[i], comms_blocking_->mem_partners_recv[i], (char ***)comms_blocking_->shmem_blocking1.small_mem, (int**)comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, (char**)sendbufs, (char**)recvbufs);
#endif
#ifdef GPU_ENABLED
    }
#endif
  } else {
    sendbufs[0] = (char*)sendbuf;
    recvbufs[0] = recvbuf;
  }
  ext_mpi_exec_blocking(comms_blocking_->comm_code_allreduce_blocking[i], 1, comms_blocking_->shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->num_sockets_per_node_blocking, (void **)comms_blocking_->shmem_blocking1.mem, sendbufs, recvbufs, ((count - 1) / comms_blocking_->padding_factor_allreduce_blocking[i] + 1) * type_size, reduction_op, ccount, comms_blocking_->p_dev_temp);
  shmem_temp = comms_blocking_->shmem_blocking1;
  comms_blocking_->shmem_blocking1 = comms_blocking_->shmem_blocking2;
  comms_blocking_->shmem_blocking2 = shmem_temp;
#ifdef GPU_ENABLED
  if (!ext_mpi_gpu_is_device_pointer(recvbuf)) {
#endif
#ifdef GPU_ENABLED
  }
#endif
  return 0;
}

int EXT_MPI_Reduce_scatter_block_native(const void *sendbuf, void *recvbuf, int recvcount, int reduction_op, int i_comm) {
//  struct comm_comm_blocking *comms_blocking_;
//  int *shmemid_temp, i = 0;
//  char **shmem_temp;
//  comms_blocking_ = comms_blocking[i_comm];
#ifdef GPU_ENABLED
//  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
//    comms_blocking_ = comms_blocking_gpu[i_comm];
//  }
#endif
//  while (comms_blocking_->count_reduce_scatter_block_blocking[i] < recvcount && comms_blocking_->comm_code_reduce_scatter_block_blocking[i + 1]) i++;
// FIXME
//  ext_mpi_exec_blocking(comms_blocking_->comm_code_reduce_scatter_block_blocking[i], comms_blocking_->comm_blocking, 1, comms_blocking_->shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->num_sockets_per_node_blocking, (void **)comms_blocking_->shmem_blocking1, sendbuf, recvbuf, recvcount / comms_blocking_->padding_factor_reduce_scatter_block_blocking[i], reduction_op, recvcount, NULL);
//  ext_mpi_exec_blocking(comms_blocking_->comm_code_reduce_scatter_block_blocking[i], comms_blocking_->comm_blocking, 1, comms_blocking_->shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->num_sockets_per_node_blocking, (void **)comms_blocking_->shmem_blocking1, sendbuf, recvbuf, recvcount / comms_blocking_->padding_factor_reduce_scatter_block_blocking[i], reduction_op, recvcount * comms_blocking_->mpi_size_blocking / comms_blocking_->num_cores_blocking, NULL, comms_blocking_->p_dev_temp);
//  shmem_temp = comms_blocking_->shmem_blocking1;
//  comms_blocking_->shmem_blocking1 = comms_blocking_->shmem_blocking2;
//  comms_blocking_->shmem_blocking2 = shmem_temp;
//  shmemid_temp = comms_blocking_->shmem_blocking_shmemid1;
//  comms_blocking_->shmem_blocking_shmemid1 = comms_blocking_->shmem_blocking_shmemid2;
//  comms_blocking_->shmem_blocking_shmemid2 = shmemid_temp;
  return 0;
}

int EXT_MPI_Allgather_native(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm, int i_comm) {
//  struct comm_comm_blocking *comms_blocking_;
//  int *shmemid_temp, i = 0;
//  char **shmem_temp;
//  comms_blocking_ = comms_blocking[i_comm];
#ifdef GPU_ENABLED
//  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
//    comms_blocking_ = comms_blocking_gpu[i_comm];
//  }
#endif
//  while (comms_blocking_->count_allgather_blocking[i] < sendcount && comms_blocking_->comm_code_allgather_blocking[i + 1]) i++;
// FIXME
//  ext_mpi_exec_blocking(comms_blocking_->comm_code_allgather_blocking[i], comms_blocking_->comm_blocking, 1, comms_blocking_->shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->num_sockets_per_node_blocking, (void **)comms_blocking_->shmem_blocking1, sendbuf, recvbuf, sendcount, -1, sendcount * comms_blocking_->mpi_size_blocking / comms_blocking_->num_cores_blocking, NULL);
//  ext_mpi_exec_blocking(comms_blocking_->comm_code_allgather_blocking[i], comms_blocking_->comm_blocking, 1, comms_blocking_->shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->num_sockets_per_node_blocking, (void **)comms_blocking_->shmem_blocking1, sendbuf, recvbuf, sendcount, -1, sendcount * comms_blocking_->mpi_size_blocking / comms_blocking_->num_cores_blocking, NULL, comms_blocking_->p_dev_temp);
//  shmem_temp = comms_blocking_->shmem_blocking1;
//  comms_blocking_->shmem_blocking1 = comms_blocking_->shmem_blocking2;
//  comms_blocking_->shmem_blocking2 = shmem_temp;
//  shmemid_temp = comms_blocking_->shmem_blocking_shmemid1;
//  comms_blocking_->shmem_blocking_shmemid1 = comms_blocking_->shmem_blocking_shmemid2;
//  comms_blocking_->shmem_blocking_shmemid2 = shmemid_temp;
  return 0;
}

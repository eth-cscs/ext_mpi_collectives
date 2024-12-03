#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
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

enum collective_subtypes {
  out_of_place = 0,
  in_place = 1,
#ifndef GPU_ENABLED
  size = 2
#else
  out_of_place_gpu = 2,
  in_place_gpu = 3,
  size = 4
#endif
};

struct comm_properties {
  int count;
  int num_sockets_per_node;
  int copyin;
  int *num_ports;
  int *groups;
  int *copyin_factors;
  char *comm_code;
  long long int *all_xpmem_id_permutated;
  char **shmem_socket_blocking;
  char **shmem_node_blocking;
  char **small_mem1;
  char **small_mem2;
  char **mem1;
  char **mem2;
#ifdef GPU_ENABLED
  char **mem_gpu1;
  char **mem_gpu2;
#endif
};

struct shmem_blocking {
  char **small_mem;
  int *small_shmemid;
  int *small_sizes;
  char **mem;
  int *shmemid;
  int *sizes;
#ifdef GPU_ENABLED
  char **mem_gpu;
  int *shmemid_gpu;
  int *sizes_gpu;
#endif
};

#define ALGORITHM_MAX 101

struct comm_comm_blocking {
  int mpi_size_blocking;
  int mpi_rank_blocking;
  int padding_factor_allreduce_blocking[ALGORITHM_MAX];
  int padding_factor_reduce_scatter_block_blocking[ALGORITHM_MAX];
  struct shmem_blocking shmem_blocking1;
  struct shmem_blocking shmem_blocking2;
  int shmem_alt;
  char locmem_blocking[1024];
  char *comm_code_allgather_blocking[ALGORITHM_MAX];
  char **shmem_socket_blocking;
  int *shmem_socket_blocking_shmemid;
  int counter_socket_blocking;
  int socket_rank_blocking;
  int num_cores_blocking;
  char **shmem_node_blocking;
  int *shmem_node_blocking_shmemid;
  int counter_node_blocking;
  void *p_dev_temp;
  int *sizes_shared_node;
  int *sizes_shared_socket;
  int *mem_partners_send_allreduce[ALGORITHM_MAX];
  int *mem_partners_recv_allreduce[ALGORITHM_MAX];
  int *mem_partners_send_reduce_scatter_block[ALGORITHM_MAX];
  int *mem_partners_recv_reduce_scatter_block[ALGORITHM_MAX];
  struct xpmem_tree **xpmem_tree_root;
  int copyin[ALGORITHM_MAX];
  int *ranks_node;
  struct comm_properties comm_property_allreduce[(enum collective_subtypes)(size)][ALGORITHM_MAX];
  struct comm_properties comm_property_reduce_scatter_block[(enum collective_subtypes)(size)][ALGORITHM_MAX];
  int initialized;
  int initialized_gpu;
};

static struct comm_comm_blocking **comms_blocking = NULL;

/*static int global_min(int i, MPI_Comm comm_row, MPI_Comm comm_column) {
  ext_mpi_call_mpi(PMPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row));
  if (comm_column != MPI_COMM_NULL) {
    ext_mpi_call_mpi(PMPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_column));
  }
  return (i);
}*/

static void get_shmem_permutation_sockets(int mpi_size, int mpi_rank, int num_sockets, int *permutation) {
  int perm[mpi_size], temp, i, j, k;
  for (i = 0; i < mpi_size; i++) {
    perm[i] = i;
  }
  if (num_sockets > 1) {
    for (j = 0; j < mpi_rank; j++) {
      temp = perm[0];
      for (i = 1; i < mpi_size; i++) {
        perm[i - 1] = perm[i];
      }
      perm[mpi_size - 1] = temp;
    }
    for (j = 0; j < mpi_rank / (mpi_size / num_sockets) * (mpi_size / num_sockets); j++) {
      temp = perm[mpi_size - 1];
      for (i = mpi_size - 1; i > 0; i--) {
        perm[i] = perm[i - 1];
      }
      perm[0] = temp;
    }
    for (k = 0; k < num_sockets; k++) {
      for (j = 0; j < mpi_rank % (mpi_size / num_sockets); j++) {
        temp = perm[mpi_size / num_sockets - 1 + k * (mpi_size / num_sockets)];
        for (i = mpi_size / num_sockets - 1; i > 0; i--) {
          perm[i + k * (mpi_size / num_sockets)] = perm[i - 1 + k * (mpi_size / num_sockets)];
        }
        perm[k * (mpi_size / num_sockets)] = temp;
      }
    }
  }
  for (i = 0; i < mpi_size; i++) {
    permutation[perm[i]] = i;
  }
}

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

static int init_blocking_native(char *filename, int ctype, MPI_Comm comm, int i_comm) {
  char data_num_ports[1000], data_copyin_factors[1000], filename_complete[1000];
  FILE *data;
  int message_size, *num_ports = NULL, *groups = NULL, copyin_method, *copyin_factors = NULL, num_sockets_per_node = 1, cores_per_node, mpi_rank, mpi_size, message_size_old = -1, on_gpu = 0, i_cprop = 0, i;
  struct comm_properties *comm_property_in_place, *comm_property_out_of_place;
#ifdef GPU_ENABLED
  struct comm_properties *comm_property_in_place_gpu, *comm_property_out_of_place_gpu;
#endif
  if (!comms_blocking) {
    comms_blocking = (struct comm_comm_blocking **)malloc(1000 * sizeof(struct comm_comm_blocking *));
    memset(comms_blocking, 0, 1000 * sizeof(struct comm_comm_blocking *));
  }
  if (!comms_blocking[i_comm]) {
    comms_blocking[i_comm] = (struct comm_comm_blocking *)malloc(sizeof(struct comm_comm_blocking));
    memset(comms_blocking[i_comm], 0, sizeof(struct comm_comm_blocking));
  }
  switch (ctype) {
    case 0:
      comm_property_in_place = comms_blocking[i_comm]->comm_property_allreduce[in_place];
      comm_property_out_of_place = comms_blocking[i_comm]->comm_property_allreduce[out_of_place];
#ifdef GPU_ENABLED
      comm_property_in_place_gpu = comms_blocking[i_comm]->comm_property_allreduce[in_place_gpu];
      comm_property_out_of_place_gpu = comms_blocking[i_comm]->comm_property_allreduce[out_of_place_gpu];
#endif
      break;
    case 1:
      comm_property_in_place = comms_blocking[i_comm]->comm_property_reduce_scatter_block[in_place];
      comm_property_out_of_place = comms_blocking[i_comm]->comm_property_reduce_scatter_block[out_of_place];
#ifdef GPU_ENABLED
      comm_property_in_place_gpu = comms_blocking[i_comm]->comm_property_reduce_scatter_block[in_place_gpu];
      comm_property_out_of_place_gpu = comms_blocking[i_comm]->comm_property_reduce_scatter_block[out_of_place_gpu];
#endif
      break;
    default:
      assert(0);
  }
  ext_mpi_call_mpi(MPI_Comm_rank(comm, &mpi_rank));
  ext_mpi_call_mpi(MPI_Comm_size(comm, &mpi_size));
  cores_per_node = ext_mpi_get_num_tasks_per_socket(comm, num_sockets_per_node);
  ext_mpi_my_cores_per_node_array[i_comm] = cores_per_node;
  sprintf(filename_complete, "/dev/shm/%s_%d_%d.txt", filename, mpi_size / cores_per_node, cores_per_node);
  data = fopen(filename_complete, "r");
  if (!data) {
    sprintf(filename_complete, "%s_%d_%d.txt", filename, mpi_size / cores_per_node, cores_per_node);
    data = fopen(filename_complete, "r");
  }
  if (!data) {
    if (mpi_rank == 0) printf("file %s missing\n", filename_complete);
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
      comm_property_in_place[i_cprop].count = message_size;
      comm_property_in_place[i_cprop].num_sockets_per_node = num_sockets_per_node;
      comm_property_in_place[i_cprop].copyin = copyin_method;
      comm_property_in_place[i_cprop].num_ports = num_ports;
      comm_property_in_place[i_cprop].groups = groups;
      comm_property_in_place[i_cprop].copyin_factors = copyin_factors;
      comm_property_in_place[i_cprop].comm_code = NULL;
      comm_property_out_of_place[i_cprop].count = message_size;
      comm_property_out_of_place[i_cprop].num_sockets_per_node = num_sockets_per_node;
      comm_property_out_of_place[i_cprop].copyin = copyin_method;
      comm_property_out_of_place[i_cprop].num_ports = num_ports;
      comm_property_out_of_place[i_cprop].groups = groups;
      comm_property_out_of_place[i_cprop].copyin_factors = copyin_factors;
      comm_property_out_of_place[i_cprop].comm_code = NULL;
#ifdef GPU_ENABLED
    } else {
      comm_property_in_place_gpu[i_cprop].count = message_size;
      comm_property_in_place_gpu[i_cprop].num_sockets_per_node = num_sockets_per_node;
      comm_property_in_place_gpu[i_cprop].copyin = copyin_method;
      comm_property_in_place_gpu[i_cprop].num_ports = num_ports;
      comm_property_in_place_gpu[i_cprop].groups = groups;
      comm_property_in_place_gpu[i_cprop].copyin_factors = copyin_factors;
      comm_property_in_place_gpu[i_cprop].comm_code = NULL;
      comm_property_out_of_place_gpu[i_cprop].count = message_size;
      comm_property_out_of_place_gpu[i_cprop].num_sockets_per_node = num_sockets_per_node;
      comm_property_out_of_place_gpu[i_cprop].copyin = copyin_method;
      comm_property_out_of_place_gpu[i_cprop].num_ports = num_ports;
      comm_property_out_of_place_gpu[i_cprop].groups = groups;
      comm_property_out_of_place_gpu[i_cprop].copyin_factors = copyin_factors;
      comm_property_out_of_place_gpu[i_cprop].comm_code = NULL;
#endif
    }
    i_cprop++;
  }
  fclose(data);
  return 0;
}

static int write_wisdom(char *filename, int mpi_size, int handle, MPI_Comm comm, int i_comm, int i_alg, int *mem_partners_send, int *mem_partners_recv, int on_gpu) {
  int rank_list[mpi_size], mem_partners[mpi_size+1], isize, rank, j;
  char *raw_code;
  FILE *data = fopen(filename, "w");
  assert(data);
  ext_mpi_call_mpi(MPI_Comm_rank(ext_mpi_COMM_WORLD_dup, &rank));
  ext_mpi_call_mpi(PMPI_Allgather(&rank, 1, MPI_INT, rank_list, 1, MPI_INT, comm));
  isize = ((struct header_byte_code*)((*e_comm_code)[handle]))->size_to_return;
  raw_code = (char*)malloc(isize * sizeof(char));
#ifdef GPU_ENABLED
  if (on_gpu) {
    isize += ((struct header_byte_code*)((*e_comm_code)[handle]))->gpu_byte_code_size;
  }
  EXT_MPI_Collective_to_disc((*e_comm_code)[handle], comms_blocking[i_comm]->locmem_blocking, rank_list, raw_code, ((struct header_byte_code*)((*e_comm_code)[handle]))->gpu_byte_code);
#else
  EXT_MPI_Collective_to_disc((*e_comm_code)[handle], comms_blocking[i_comm]->locmem_blocking, rank_list, raw_code, NULL);
#endif
  fwrite(&isize, sizeof(int), 1, data);
  if (mem_partners_send) {
    for (j = 0; mem_partners_send[j] >= 0; j++) {
      mem_partners[j] = mem_partners_send[j];
    }
    mem_partners[j] = mem_partners_send[j];
  } else {
    mem_partners[0] = -1;
  }
  fwrite(mem_partners, sizeof(int), comms_blocking[i_comm]->mpi_size_blocking + 1, data);
  if (mem_partners_recv) {
    for (j = 0; mem_partners_recv[j] >= 0; j++) {
      mem_partners[j] = mem_partners_recv[j];
    }
    mem_partners[j] = mem_partners_recv[j];
  } else {
    mem_partners[0] = -1;
  }
  fwrite(mem_partners, sizeof(int), comms_blocking[i_comm]->mpi_size_blocking + 1, data);
  fwrite(raw_code, sizeof(char), ((struct header_byte_code*)((*e_comm_code)[handle]))->size_to_return, data);
#ifdef GPU_ENABLED
  if (on_gpu) {
    fwrite(((struct header_byte_code*)((*e_comm_code)[handle]))->gpu_byte_code, sizeof(char), ((struct header_byte_code*)((*e_comm_code)[handle]))->gpu_byte_code_size, data);
  }
#endif
  free(raw_code);
  fclose(data);
  return 0;
}

static int read_wisdom(char *filename, int mpi_size, MPI_Comm comm, int i_comm, int i_alg, int **mem_partners_send, int **mem_partners_recv, int on_gpu) {
  struct header_byte_code *header;
  int rank_list[mpi_size], mem_partners[mpi_size + 1], isize, handle, j;
  char *raw_code;
#ifdef GPU_ENABLED
  char *p_gpu;
#endif
  FILE *data = fopen(filename, "r");
  if (!data) return -1;
  assert(fread(&isize, sizeof(int), 1, data) == 1);
  assert(fread(mem_partners, sizeof(int), comms_blocking[i_comm]->mpi_size_blocking + 1, data) == comms_blocking[i_comm]->mpi_size_blocking + 1);
  *mem_partners_send = (int*)malloc((comms_blocking[i_comm]->mpi_size_blocking + 1) * sizeof(int));
  for (j = 0; mem_partners[j] >= 0; j++) {
    (*mem_partners_send)[j] = mem_partners[j];
  }
  (*mem_partners_send)[j] = mem_partners[j];
  assert(fread(mem_partners, sizeof(int), comms_blocking[i_comm]->mpi_size_blocking + 1, data) == comms_blocking[i_comm]->mpi_size_blocking + 1);
  *mem_partners_recv = (int*)malloc((comms_blocking[i_comm]->mpi_size_blocking + 1) * sizeof(int));
  for (j = 0; mem_partners[j] >= 0; j++) {
    (*mem_partners_recv)[j] = mem_partners[j];
  }
  (*mem_partners_recv)[j] = mem_partners[j];
  raw_code = (char*)malloc(isize * sizeof(char));
  assert(fread(raw_code, sizeof(char), isize, data) == isize);
  handle = EXT_MPI_Get_handle();
#ifdef GPU_ENABLED
  if (on_gpu) {
    p_gpu = (char*)malloc(((struct header_byte_code*)raw_code)->gpu_byte_code_size);
  } else {
    p_gpu = NULL;
  }
  (*e_comm_code)[handle] = EXT_MPI_Collective_from_disc(raw_code, comms_blocking[i_comm]->locmem_blocking, rank_list, p_gpu);
#else
  (*e_comm_code)[handle] = EXT_MPI_Collective_from_disc(raw_code, comms_blocking[i_comm]->locmem_blocking, rank_list, NULL);
#endif
  header = (struct header_byte_code*)((*e_comm_code)[handle]);
  header->barrier_shmem_node = NULL;
  header->barrier_shmem_socket = NULL;
  header->barrier_shmem_socket_small = NULL;
  (*e_comm_code)[handle + 1] = NULL;
#ifdef GPU_ENABLED
  if (on_gpu) {
    header->gpu_byte_code = p_gpu;
    memcpy(header->gpu_byte_code, raw_code + header->size_to_return, header->gpu_byte_code_size);
  }
#endif
  free(raw_code);
  fclose(data);
  return handle;
}

static void assign_permutation(int i_comm, struct comm_properties *comm_property) {
  int permutation[comms_blocking[i_comm]->mpi_size_blocking], i;
  comm_property->shmem_socket_blocking = (char**)malloc(comms_blocking[i_comm]->mpi_size_blocking * 6 * sizeof(char*));
  comm_property->shmem_node_blocking = comm_property->shmem_socket_blocking + comms_blocking[i_comm]->mpi_size_blocking;
  comm_property->small_mem1 = comm_property->shmem_socket_blocking + 2 * comms_blocking[i_comm]->mpi_size_blocking;
  comm_property->small_mem2 = comm_property->shmem_socket_blocking + 3 * comms_blocking[i_comm]->mpi_size_blocking;
  comm_property->mem1 = comm_property->shmem_socket_blocking + 4 * comms_blocking[i_comm]->mpi_size_blocking;
  comm_property->mem2 = comm_property->shmem_socket_blocking + 5 * comms_blocking[i_comm]->mpi_size_blocking;
  get_shmem_permutation_sockets(comms_blocking[i_comm]->mpi_size_blocking, comms_blocking[i_comm]->mpi_rank_blocking, comm_property->num_sockets_per_node, permutation);
  for (i = 0; i < comms_blocking[i_comm]->mpi_size_blocking; i++) {
    comm_property->shmem_socket_blocking[i] = comms_blocking[i_comm]->shmem_socket_blocking[permutation[i]];
    comm_property->shmem_node_blocking[i] = comms_blocking[i_comm]->shmem_node_blocking[permutation[i]];
    comm_property->small_mem1[i] = comms_blocking[i_comm]->shmem_blocking1.small_mem[permutation[i]];
    comm_property->small_mem2[i] = comms_blocking[i_comm]->shmem_blocking2.small_mem[permutation[i]];
    comm_property->mem1[i] = comms_blocking[i_comm]->shmem_blocking1.mem[permutation[i]];
    comm_property->mem2[i] = comms_blocking[i_comm]->shmem_blocking2.mem[permutation[i]];
  }
}

#ifdef GPU_ENABLED
static void assign_permutation_gpu(int i_comm, struct comm_properties *comm_property) {
  int permutation[comms_blocking[i_comm]->mpi_size_blocking], i;
  comm_property->mem_gpu1 = (char**)malloc(comms_blocking[i_comm]->mpi_size_blocking * 2 * sizeof(char*));
  comm_property->mem_gpu2 = comm_property->mem_gpu1 + comms_blocking[i_comm]->mpi_size_blocking;
  get_shmem_permutation_sockets(comms_blocking[i_comm]->mpi_size_blocking, comms_blocking[i_comm]->mpi_rank_blocking, comm_property->num_sockets_per_node, permutation);
  for (i = 0; i < comms_blocking[i_comm]->mpi_size_blocking; i++) {
    comm_property->mem_gpu1[i] = comms_blocking[i_comm]->shmem_blocking1.mem_gpu[permutation[i]];
    comm_property->mem_gpu2[i] = comms_blocking[i_comm]->shmem_blocking2.mem_gpu[permutation[i]];
  }
}
#endif

static int add_blocking_native(int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, int my_cores_per_node, int *num_ports, int *groups, int copyin, int *copyin_factors, int bit, int recursive, int arecursive, int blocking, enum ecollective_type collective_type, int i_comm, enum collective_subtypes collective_subtype, long int send_ptr, long int recv_ptr) {
  MPI_Comm comm_node;
  int handle, size_shared = 1024*1024, *recvcounts, *displs, padding_factor, type_size, my_mpi_size, my_mpi_rank, rank, flag, i, j, k;
  char filename[1000];
  ext_mpi_call_mpi(MPI_Type_size(datatype, &type_size));
  if (!comms_blocking[i_comm]->initialized) {
    comms_blocking[i_comm]->initialized = 1;
#ifdef GPU_ENABLED
    ext_mpi_gpu_malloc(&comms_blocking[i_comm]->p_dev_temp, 10000);
#endif
    ext_mpi_call_mpi(MPI_Comm_size(comm, &comms_blocking[i_comm]->mpi_size_blocking));
    ext_mpi_call_mpi(MPI_Comm_rank(comm, &comms_blocking[i_comm]->mpi_rank_blocking));
    comms_blocking[i_comm]->ranks_node = (int*)malloc(my_cores_per_node * sizeof(int));
    ext_mpi_call_mpi(PMPI_Comm_size(comm, &my_mpi_size));
    ext_mpi_call_mpi(PMPI_Comm_rank(comm, &my_mpi_rank));
    ext_mpi_call_mpi(PMPI_Comm_split(comm, my_mpi_rank / my_cores_per_node, my_mpi_rank % my_cores_per_node, &comm_node));
    ext_mpi_call_mpi(MPI_Comm_rank(ext_mpi_COMM_WORLD_dup, &i));
    ext_mpi_call_mpi(PMPI_Allgather(&i, 1, MPI_INT, comms_blocking[i_comm]->ranks_node, 1, MPI_INT, comm_node));
    ext_mpi_call_mpi(PMPI_Comm_free(&comm_node));
#ifdef XPMEM
    ext_mpi_init_xpmem_blocking(comm, &comms_blocking[i_comm]->xpmem_tree_root);
#endif
    ext_mpi_setup_shared_memory(comm, my_cores_per_node, 1, size_shared, &comms_blocking[i_comm]->sizes_shared_socket, &comms_blocking[i_comm]->shmem_socket_blocking_shmemid, &comms_blocking[i_comm]->shmem_socket_blocking);
    comms_blocking[i_comm]->counter_socket_blocking = 0;
    comms_blocking[i_comm]->num_cores_blocking = my_cores_per_node;
    comms_blocking[i_comm]->socket_rank_blocking = comms_blocking[i_comm]->mpi_rank_blocking % comms_blocking[i_comm]->num_cores_blocking;
    ext_mpi_setup_shared_memory(comm, my_cores_per_node, 1, size_shared, &comms_blocking[i_comm]->sizes_shared_node, &comms_blocking[i_comm]->shmem_node_blocking_shmemid, &comms_blocking[i_comm]->shmem_node_blocking);
    comms_blocking[i_comm]->counter_node_blocking = 0;
#ifdef GPU_ENABLED
    ext_mpi_setup_shared_memory(comm, my_cores_per_node, 1, 2 * sizeof(struct address_transfer), &comms_blocking[i_comm]->shmem_blocking1.small_sizes, &comms_blocking[i_comm]->shmem_blocking1.small_shmemid, &comms_blocking[i_comm]->shmem_blocking1.small_mem);
    ext_mpi_setup_shared_memory(comm, my_cores_per_node, 1, 2 * sizeof(struct address_transfer), &comms_blocking[i_comm]->shmem_blocking2.small_sizes, &comms_blocking[i_comm]->shmem_blocking2.small_shmemid, &comms_blocking[i_comm]->shmem_blocking2.small_mem);
#else
#ifdef XPMEM
    ext_mpi_setup_shared_memory(comm, my_cores_per_node, 1, 2 * sizeof(void*), &comms_blocking[i_comm]->shmem_blocking1.small_sizes, &comms_blocking[i_comm]->shmem_blocking1.small_shmemid, &comms_blocking[i_comm]->shmem_blocking1.small_mem);
    ext_mpi_setup_shared_memory(comm, my_cores_per_node, 1, 2 * sizeof(void*), &comms_blocking[i_comm]->shmem_blocking2.small_sizes, &comms_blocking[i_comm]->shmem_blocking2.small_shmemid, &comms_blocking[i_comm]->shmem_blocking2.small_mem);
#endif
#endif
    size_shared = 1000 * 1024 * 1024 / 8;
    ext_mpi_setup_shared_memory(comm, my_cores_per_node, 1, size_shared, &comms_blocking[i_comm]->shmem_blocking1.sizes, &comms_blocking[i_comm]->shmem_blocking1.shmemid, &comms_blocking[i_comm]->shmem_blocking1.mem);
    ext_mpi_setup_shared_memory(comm, my_cores_per_node, 1, size_shared, &comms_blocking[i_comm]->shmem_blocking2.sizes, &comms_blocking[i_comm]->shmem_blocking2.shmemid, &comms_blocking[i_comm]->shmem_blocking2.mem);
  }
#ifdef GPU_ENABLED
  if (recv_ptr == RECV_PTR_GPU && !comms_blocking[i_comm]->initialized_gpu) {
    comms_blocking[i_comm]->initialized_gpu = 1;
    ext_mpi_gpu_setup_shared_memory(comm, my_cores_per_node, size_shared, 1, &comms_blocking[i_comm]->shmem_blocking1.shmemid_gpu, &comms_blocking[i_comm]->shmem_blocking1.mem_gpu);
    ext_mpi_gpu_setup_shared_memory(comm, my_cores_per_node, size_shared, 1, &comms_blocking[i_comm]->shmem_blocking2.shmemid_gpu, &comms_blocking[i_comm]->shmem_blocking2.mem_gpu);
  }
#endif
  switch (collective_type) {
    case collective_type_allreduce:
      for (i = 0; comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i].comm_code; i++)
        ;
      ext_mpi_xpmem_blocking_get_id_permutated(comm, comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i].num_sockets_per_node, &comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i].all_xpmem_id_permutated);
      if (comms_blocking[i_comm]->mpi_size_blocking < count) {
        comms_blocking[i_comm]->padding_factor_allreduce_blocking[i] = comms_blocking[i_comm]->mpi_size_blocking;
	j = comms_blocking[i_comm]->mpi_size_blocking;
      } else {
        comms_blocking[i_comm]->padding_factor_allreduce_blocking[i] = count;
	j = count;
      }
      assign_permutation(i_comm, &comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i]);
#ifdef GPU_ENABLED
      if (recv_ptr == RECV_PTR_GPU && comms_blocking[i_comm]->initialized_gpu) {
	assign_permutation_gpu(i_comm, &comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i]);
      }
#endif
      if (1) {
        ext_mpi_call_mpi(MPI_Comm_rank(ext_mpi_COMM_WORLD_dup, &rank));
        sprintf(filename, "/dev/shm/ext_mpi_allreduce_blocking_%d_%d_%d_%d_%d_%d.dat", comms_blocking[i_comm]->mpi_size_blocking / my_cores_per_node, my_cores_per_node, count, (send_ptr == recv_ptr) + 2 * (recv_ptr != RECV_PTR_CPU), comms_blocking[i_comm]->mpi_rank_blocking, rank);
        handle = read_wisdom(filename, comms_blocking[i_comm]->mpi_size_blocking, comm, i_comm, i, &comms_blocking[i_comm]->mem_partners_send_allreduce[i], &comms_blocking[i_comm]->mem_partners_recv_allreduce[i], recv_ptr != RECV_PTR_CPU);
        flag = handle < 0;
        ext_mpi_call_mpi(PMPI_Allreduce(MPI_IN_PLACE, &flag, 1, MPI_INT, MPI_MAX, comm));
        if (flag) {
          handle = EXT_MPI_Allreduce_init_native((char *)(send_ptr), (char *)(recv_ptr), j, datatype, op, comm, my_cores_per_node / comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i].num_sockets_per_node, MPI_COMM_NULL, 1, num_ports, groups, copyin, copyin_factors, 1, bit, 0, 0, 0, comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i].num_sockets_per_node, 1, comms_blocking[i_comm]->locmem_blocking, &padding_factor, &comms_blocking[i_comm]->mem_partners_send_allreduce[i], &comms_blocking[i_comm]->mem_partners_recv_allreduce[i]);
          write_wisdom(filename, comms_blocking[i_comm]->mpi_size_blocking, handle, comm, i_comm, i, comms_blocking[i_comm]->mem_partners_send_allreduce[i], comms_blocking[i_comm]->mem_partners_recv_allreduce[i], recv_ptr != RECV_PTR_CPU);
	}
      } else {
        handle = EXT_MPI_Allreduce_init_native((char *)(send_ptr), (char *)(recv_ptr), j, datatype, op, comm, my_cores_per_node, MPI_COMM_NULL, 1, num_ports, groups, copyin, copyin_factors, 0, bit, 0, 0, 0, comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i].num_sockets_per_node, 1, comms_blocking[i_comm]->locmem_blocking, &padding_factor, &comms_blocking[i_comm]->mem_partners_send_allreduce[i], &comms_blocking[i_comm]->mem_partners_recv_allreduce[i]);
      }
      padding_factor = 1;
      add_blocking_member(count, datatype, handle, &comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i].comm_code, &comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i].count, padding_factor, comm, 1, comms_blocking[i_comm]->copyin, copyin);
    break;
    case collective_type_reduce_scatter_block:
      for (i = 0; comms_blocking[i_comm]->comm_property_reduce_scatter_block[collective_subtype][i].comm_code; i++)
        ;
      ext_mpi_xpmem_blocking_get_id_permutated(comm, comms_blocking[i_comm]->comm_property_reduce_scatter_block[collective_subtype][i].num_sockets_per_node, &comms_blocking[i_comm]->comm_property_reduce_scatter_block[collective_subtype][i].all_xpmem_id_permutated);
      if (comms_blocking[i_comm]->mpi_size_blocking < count) {
        comms_blocking[i_comm]->padding_factor_reduce_scatter_block_blocking[i] = comms_blocking[i_comm]->mpi_size_blocking;
	j = comms_blocking[i_comm]->mpi_size_blocking;
      } else {
        comms_blocking[i_comm]->padding_factor_reduce_scatter_block_blocking[i] = count;
	j = count;
      }
      comms_blocking[i_comm]->padding_factor_reduce_scatter_block_blocking[i] = 1;
      assign_permutation(i_comm, &comms_blocking[i_comm]->comm_property_reduce_scatter_block[collective_subtype][i]);
      ext_mpi_call_mpi(MPI_Comm_rank(ext_mpi_COMM_WORLD_dup, &rank));
      sprintf(filename, "/dev/shm/ext_mpi_reduce_scatter_block_blocking_%d_%d_%d_%d_%d_%d.dat", comms_blocking[i_comm]->mpi_size_blocking / my_cores_per_node, my_cores_per_node, count, (send_ptr == recv_ptr) + 2 * (recv_ptr != RECV_PTR_CPU), comms_blocking[i_comm]->mpi_rank_blocking, rank);
      handle = read_wisdom(filename, comms_blocking[i_comm]->mpi_size_blocking, comm, i_comm, i, &comms_blocking[i_comm]->mem_partners_send_reduce_scatter_block[i], &comms_blocking[i_comm]->mem_partners_recv_reduce_scatter_block[i], recv_ptr != RECV_PTR_CPU);
      flag = handle < 0;
      ext_mpi_call_mpi(PMPI_Allreduce(MPI_IN_PLACE, &flag, 1, MPI_INT, MPI_MAX, comm));
      if (flag) {
        recvcounts = (int*)malloc(comms_blocking[i_comm]->mpi_size_blocking * sizeof(int));
        for (k = 0; k < comms_blocking[i_comm]->mpi_size_blocking; k++) {
          recvcounts[k] = j;
        }
        handle = EXT_MPI_Reduce_scatter_init_native((char *)(send_ptr), (char *)(recv_ptr), recvcounts, datatype, op, comm, my_cores_per_node / comms_blocking[i_comm]->comm_property_reduce_scatter_block[collective_subtype][i].num_sockets_per_node, MPI_COMM_NULL, 1, num_ports, groups, copyin, copyin_factors, 1, 0, 0, comms_blocking[i_comm]->comm_property_reduce_scatter_block[collective_subtype][i].num_sockets_per_node, 1, comms_blocking[i_comm]->locmem_blocking, &padding_factor, &comms_blocking[i_comm]->mem_partners_send_reduce_scatter_block[i]);
        free(recvcounts);
        write_wisdom(filename, comms_blocking[i_comm]->mpi_size_blocking, handle, comm, i_comm, i, comms_blocking[i_comm]->mem_partners_send_reduce_scatter_block[i], comms_blocking[i_comm]->mem_partners_recv_reduce_scatter_block[i], recv_ptr != RECV_PTR_CPU);
      }
      padding_factor = 1;
      add_blocking_member(count, datatype, handle, &comms_blocking[i_comm]->comm_property_reduce_scatter_block[collective_subtype][i].comm_code, &comms_blocking[i_comm]->comm_property_reduce_scatter_block[collective_subtype][i].count, padding_factor, comm, 1, comms_blocking[i_comm]->copyin, copyin);
    break;
    case collective_type_allgather:
      recvcounts = (int *)malloc(comms_blocking[i_comm]->mpi_size_blocking * sizeof(int));
      displs = (int *)malloc(comms_blocking[i_comm]->mpi_size_blocking * sizeof(int));
      for (i = 0; i < comms_blocking[i_comm]->mpi_size_blocking; i++) {
        recvcounts[i] = 1;
        displs[i] = i;
      }
//        handle = EXT_MPI_Allgatherv_init_native((char *)(send_ptr), 1, datatype, (char *)(recv_ptr), recvcounts, displs, datatype, comm, my_cores_per_node, MPI_COMM_NULL, 1, num_ports, groups, 0, arecursive, 0, num_sockets_per_node, 1, comms_blocking[i_comm]->locmem_blocking);
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
  return 0;
}

static int add_all_blocking_native(int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, int my_cores_per_node, int *num_ports, int *groups, int copyin, int *copyin_factors, int bit, int recursive, int arecursive, int blocking, enum ecollective_type collective_type, enum collective_subtypes collective_subtype, int i_comm) {
  ext_mpi_native_export(&e_handle_code_max, &e_comm_code, &e_execution_pointer, &e_active_wait, &e_is_initialised, &e_EXT_MPI_COMM_WORLD, &e_tag_max);
  switch (collective_subtype) {
    case out_of_place:
      add_blocking_native(count, datatype, op, comm, my_cores_per_node, num_ports, groups, copyin, copyin_factors, bit, recursive, arecursive, blocking, collective_type, i_comm, collective_subtype, SEND_PTR_CPU, RECV_PTR_CPU);
      break;
    case in_place:
      add_blocking_native(count, datatype, op, comm, my_cores_per_node, num_ports, groups, copyin, copyin_factors, bit, recursive, arecursive, blocking, collective_type, i_comm, collective_subtype, RECV_PTR_CPU, RECV_PTR_CPU);
      break;
#ifdef GPU_ENABLED
    case out_of_place_gpu:
      add_blocking_native(count, datatype, op, comm, my_cores_per_node, num_ports, groups, copyin, copyin_factors, bit, recursive, arecursive, blocking, collective_type, i_comm, collective_subtype, SEND_PTR_GPU, RECV_PTR_GPU);
      break;
    case in_place_gpu:
      add_blocking_native(count, datatype, op, comm, my_cores_per_node, num_ports, groups, copyin, copyin_factors, bit, recursive, arecursive, blocking, collective_type, i_comm, collective_subtype, RECV_PTR_GPU, RECV_PTR_GPU);
      break;
#endif
    default:
      printf("error in add_all_blocking_native file ext_mpi_native_blocking.c");
      exit(1);
  }
  return 0;
}

static int release_blocking_native(int i_comm, enum collective_subtypes collective_subtype, int in_place) {
  struct header_byte_code *header;
  int i;
  if (comms_blocking && comms_blocking[i_comm]) {
    if (!in_place) {
      for (i = 0; comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i].count; i++) {
        free(comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i].num_ports);
        free(comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i].groups);
        free(comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i].copyin_factors);
        free(comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i].shmem_socket_blocking);
      }
      for (i = 0; comms_blocking[i_comm]->comm_property_reduce_scatter_block[collective_subtype][i].count; i++) {
        free(comms_blocking[i_comm]->comm_property_reduce_scatter_block[collective_subtype][i].num_ports);
        free(comms_blocking[i_comm]->comm_property_reduce_scatter_block[collective_subtype][i].groups);
        free(comms_blocking[i_comm]->comm_property_reduce_scatter_block[collective_subtype][i].copyin_factors);
        free(comms_blocking[i_comm]->comm_property_reduce_scatter_block[collective_subtype][i].shmem_socket_blocking);
      }
    }
    ext_mpi_destroy_shared_memory(comms_blocking[i_comm]->num_cores_blocking, comms_blocking[i_comm]->ranks_node, comms_blocking[i_comm]->shmem_blocking1.sizes, comms_blocking[i_comm]->shmem_blocking1.shmemid, comms_blocking[i_comm]->shmem_blocking1.mem);
    ext_mpi_destroy_shared_memory(comms_blocking[i_comm]->num_cores_blocking, comms_blocking[i_comm]->ranks_node, comms_blocking[i_comm]->shmem_blocking2.sizes, comms_blocking[i_comm]->shmem_blocking2.shmemid, comms_blocking[i_comm]->shmem_blocking2.mem);
#ifdef GPU_ENABLED
    if (comms_blocking[i_comm]->shmem_blocking1.mem_gpu) {
      ext_mpi_gpu_destroy_shared_memory(comms_blocking[i_comm]->num_cores_blocking, comms_blocking[i_comm]->ranks_node, comms_blocking[i_comm]->shmem_blocking1.shmemid_gpu, comms_blocking[i_comm]->shmem_blocking1.mem_gpu);
    }
    if (comms_blocking[i_comm]->shmem_blocking2.mem_gpu) {
      ext_mpi_gpu_destroy_shared_memory(comms_blocking[i_comm]->num_cores_blocking, comms_blocking[i_comm]->ranks_node, comms_blocking[i_comm]->shmem_blocking2.shmemid_gpu, comms_blocking[i_comm]->shmem_blocking2.mem_gpu);
    }
#endif
#if defined XPMEM || defined GPU_ENABLED
    ext_mpi_destroy_shared_memory(comms_blocking[i_comm]->num_cores_blocking, comms_blocking[i_comm]->ranks_node, comms_blocking[i_comm]->shmem_blocking1.small_sizes, comms_blocking[i_comm]->shmem_blocking1.small_shmemid, comms_blocking[i_comm]->shmem_blocking1.small_mem);
    ext_mpi_destroy_shared_memory(comms_blocking[i_comm]->num_cores_blocking, comms_blocking[i_comm]->ranks_node, comms_blocking[i_comm]->shmem_blocking2.small_sizes, comms_blocking[i_comm]->shmem_blocking2.small_shmemid, comms_blocking[i_comm]->shmem_blocking2.small_mem);
#endif
    ext_mpi_destroy_shared_memory(comms_blocking[i_comm]->num_cores_blocking, comms_blocking[i_comm]->ranks_node, comms_blocking[i_comm]->sizes_shared_socket, comms_blocking[i_comm]->shmem_socket_blocking_shmemid, comms_blocking[i_comm]->shmem_socket_blocking);
    ext_mpi_destroy_shared_memory(comms_blocking[i_comm]->num_cores_blocking, comms_blocking[i_comm]->ranks_node, comms_blocking[i_comm]->sizes_shared_node, comms_blocking[i_comm]->shmem_node_blocking_shmemid, comms_blocking[i_comm]->shmem_node_blocking);
    for (i = 0; i < ALGORITHM_MAX; i++) {
      header = (struct header_byte_code*)comms_blocking[i_comm]->comm_property_allreduce[collective_subtype][i].comm_code;
      if (header) {
        free(header->barrier_shmem_node);
        free(header->barrier_shmem_socket);
        free(header->barrier_shmem_socket_small);
      }
      free(header);
    }
#ifdef XPMEM
    ext_mpi_done_xpmem_blocking(comms_blocking[i_comm]->num_cores_blocking, comms_blocking[i_comm]->xpmem_tree_root);
#endif
    free(comms_blocking[i_comm]->ranks_node);
#ifdef GPU_ENABLED
    ext_mpi_gpu_free(comms_blocking[i_comm]->p_dev_temp);
#endif
    free(comms_blocking[i_comm]);
    comms_blocking[i_comm] = NULL;
  }
  if (i_comm == 0 && collective_subtype == (enum collective_subtypes)(size) - 1) {
    free(comms_blocking);
    comms_blocking = NULL;
  }
  return 0;
}

int EXT_MPI_Release_blocking_native(int i_comm) {
  enum collective_subtypes collective_subtype;
  for (collective_subtype = 0; collective_subtype < (enum collective_subtypes)(size); collective_subtype++) {
#ifndef GPU_ENABLED
    release_blocking_native(i_comm, collective_subtype, collective_subtype == in_place);
#else
    release_blocking_native(i_comm, collective_subtype, collective_subtype == in_place || collective_subtype == in_place_gpu);
#endif
  }
  return 0;
}

int EXT_MPI_Allreduce_native(const void *sendbuf, void *recvbuf, int count, int reduction_op, int i_comm) {
  enum collective_subtypes collective_subtype;
  struct comm_comm_blocking *comms_blocking_;
  int type_size, ccount, i = 0;
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
  if (!comms_blocking || !comms_blocking[i_comm] || !comms_blocking[i_comm]->comm_property_allreduce[0][0].num_sockets_per_node) {
    init_blocking_native("ext_mpi_allreduce_blocking", 0, ext_mpi_comm_array[i_comm], i_comm);
  }
  comms_blocking_ = comms_blocking[i_comm];
  while (comms_blocking_->comm_property_allreduce[collective_subtype][i].count <= ccount && comms_blocking_->comm_property_allreduce[collective_subtype][i + 1].comm_code) i++;
  if (comms_blocking_->comm_property_allreduce[collective_subtype][i].count > ccount && i > 0) i--;
  if (!comms_blocking_->comm_property_allreduce[collective_subtype][i].comm_code) {
    add_all_blocking_native(comms_blocking_->comm_property_allreduce[collective_subtype][i].count / sizeof(long int), MPI_LONG, MPI_SUM, ext_mpi_comm_array[i_comm], ext_mpi_my_cores_per_node_array[i_comm], comms_blocking_->comm_property_allreduce[collective_subtype][i].num_ports, comms_blocking_->comm_property_allreduce[collective_subtype][i].groups, comms_blocking_->comm_property_allreduce[collective_subtype][i].copyin, comms_blocking_->comm_property_allreduce[collective_subtype][i].copyin_factors, 0, 1, 1, 1, collective_type_allreduce, collective_subtype, i_comm);
  }
  if (comms_blocking_->copyin[i] < 8) {
#ifdef GPU_ENABLED
    if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
      if (!comms_blocking_->shmem_alt) {
        ext_mpi_sendrecvbuf_init_gpu_blocking(comms_blocking_->mpi_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_allreduce[collective_subtype][i].num_sockets_per_node, (char*)sendbuf, recvbuf, ccount, comms_blocking_->mem_partners_send_allreduce[i], comms_blocking_->mem_partners_recv_allreduce[i], (char ***)comms_blocking_->comm_property_allreduce[collective_subtype][i].small_mem1, (int**)comms_blocking_->comm_property_allreduce[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, (char**)sendbufs, (char**)recvbufs);
      } else {
        ext_mpi_sendrecvbuf_init_gpu_blocking(comms_blocking_->mpi_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_allreduce[collective_subtype][i].num_sockets_per_node, (char*)sendbuf, recvbuf, ccount, comms_blocking_->mem_partners_send_allreduce[i], comms_blocking_->mem_partners_recv_allreduce[i], (char ***)comms_blocking_->comm_property_allreduce[collective_subtype][i].small_mem2, (int**)comms_blocking_->comm_property_allreduce[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, (char**)sendbufs, (char**)recvbufs);
      }
    } else {
#endif
#ifdef XPMEM
      if (!comms_blocking_->shmem_alt) {
        ext_mpi_sendrecvbuf_init_xpmem_blocking(comms_blocking_->xpmem_tree_root, comms_blocking_->mpi_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_allreduce[collective_subtype][i].num_sockets_per_node, (char*)sendbuf, recvbuf, ccount, comms_blocking_->comm_property_allreduce[collective_subtype][i].all_xpmem_id_permutated, comms_blocking_->mem_partners_send_allreduce[i], comms_blocking_->mem_partners_recv_allreduce[i], (char ***)comms_blocking_->comm_property_allreduce[collective_subtype][i].small_mem1, (int**)comms_blocking_->comm_property_allreduce[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, (char**)sendbufs, (char**)recvbufs);
      } else {
        ext_mpi_sendrecvbuf_init_xpmem_blocking(comms_blocking_->xpmem_tree_root, comms_blocking_->mpi_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_allreduce[collective_subtype][i].num_sockets_per_node, (char*)sendbuf, recvbuf, ccount, comms_blocking_->comm_property_allreduce[collective_subtype][i].all_xpmem_id_permutated, comms_blocking_->mem_partners_send_allreduce[i], comms_blocking_->mem_partners_recv_allreduce[i], (char ***)comms_blocking_->comm_property_allreduce[collective_subtype][i].small_mem2, (int**)comms_blocking_->comm_property_allreduce[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, (char**)sendbufs, (char**)recvbufs);
      }
#endif
#ifdef GPU_ENABLED
    }
#endif
  } else {
    sendbufs[0] = (char*)sendbuf;
    recvbufs[0] = recvbuf;
  }
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
    if (!comms_blocking_->shmem_alt) {
      ext_mpi_exec_blocking(comms_blocking_->comm_property_allreduce[collective_subtype][i].comm_code, 1, comms_blocking_->comm_property_allreduce[collective_subtype][i].shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_allreduce[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->comm_property_allreduce[collective_subtype][i].num_sockets_per_node, (void **)comms_blocking_->comm_property_allreduce[collective_subtype][i].mem_gpu1, sendbufs, recvbufs, ((count - 1) / comms_blocking_->padding_factor_allreduce_blocking[i] + 1) * type_size, reduction_op, ccount, comms_blocking_->p_dev_temp);
    } else {
      ext_mpi_exec_blocking(comms_blocking_->comm_property_allreduce[collective_subtype][i].comm_code, 1, comms_blocking_->comm_property_allreduce[collective_subtype][i].shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_allreduce[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->comm_property_allreduce[collective_subtype][i].num_sockets_per_node, (void **)comms_blocking_->comm_property_allreduce[collective_subtype][i].mem_gpu2, sendbufs, recvbufs, ((count - 1) / comms_blocking_->padding_factor_allreduce_blocking[i] + 1) * type_size, reduction_op, ccount, comms_blocking_->p_dev_temp);
    }
  } else {
#endif
    if (!comms_blocking_->shmem_alt) {
      ext_mpi_exec_blocking(comms_blocking_->comm_property_allreduce[collective_subtype][i].comm_code, 1, comms_blocking_->comm_property_allreduce[collective_subtype][i].shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_allreduce[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->comm_property_allreduce[collective_subtype][i].num_sockets_per_node, (void **)comms_blocking_->comm_property_allreduce[collective_subtype][i].mem1, sendbufs, recvbufs, ((count - 1) / comms_blocking_->padding_factor_allreduce_blocking[i] + 1) * type_size, reduction_op, ccount, comms_blocking_->p_dev_temp);
    } else {
      ext_mpi_exec_blocking(comms_blocking_->comm_property_allreduce[collective_subtype][i].comm_code, 1, comms_blocking_->comm_property_allreduce[collective_subtype][i].shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_allreduce[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->comm_property_allreduce[collective_subtype][i].num_sockets_per_node, (void **)comms_blocking_->comm_property_allreduce[collective_subtype][i].mem2, sendbufs, recvbufs, ((count - 1) / comms_blocking_->padding_factor_allreduce_blocking[i] + 1) * type_size, reduction_op, ccount, comms_blocking_->p_dev_temp);
    }
#ifdef GPU_ENABLED
  }
#endif
  comms_blocking_->shmem_alt = 1 - comms_blocking_->shmem_alt;
  return 0;
}

int EXT_MPI_Reduce_scatter_block_native(const void *sendbuf, void *recvbuf, int recvcount, int reduction_op, int i_comm) {
  enum collective_subtypes collective_subtype;
  struct comm_comm_blocking *comms_blocking_;
  int type_size, ccount, i = 0;
  void *sendbufs[0x1000], *recvbufs[0x1000];
  type_size = get_type_size(reduction_op);
  ccount = recvcount * type_size;
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
  if (!comms_blocking || !comms_blocking[i_comm] || !comms_blocking[i_comm]->comm_property_reduce_scatter_block[0][0].num_sockets_per_node) {
    init_blocking_native("ext_mpi_reduce_scatter_block_blocking", 1, ext_mpi_comm_array[i_comm], i_comm);
  }
  comms_blocking_ = comms_blocking[i_comm];
  while (comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].count <= ccount && comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i + 1].comm_code) i++;
  if (comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].count > ccount && i > 0) i--;
  if (!comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].comm_code) {
    add_all_blocking_native(comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].count / sizeof(long int), MPI_LONG, MPI_SUM, ext_mpi_comm_array[i_comm], ext_mpi_my_cores_per_node_array[i_comm], comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].num_ports, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].groups, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].copyin, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].copyin_factors, 0, 1, 1, 1, collective_type_reduce_scatter_block, collective_subtype, i_comm);
  }
  if (comms_blocking_->copyin[i] < 8) {
#ifdef GPU_ENABLED
    if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
      if (!comms_blocking_->shmem_alt) {
        ext_mpi_sendrecvbuf_init_gpu_blocking(comms_blocking_->mpi_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].num_sockets_per_node, (char*)sendbuf, recvbuf, ccount, comms_blocking_->mem_partners_send_reduce_scatter_block[i], comms_blocking_->mem_partners_recv_reduce_scatter_block[i], (char ***)comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].small_mem1, (int**)comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, (char**)sendbufs, (char**)recvbufs);
      } else {
        ext_mpi_sendrecvbuf_init_gpu_blocking(comms_blocking_->mpi_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].num_sockets_per_node, (char*)sendbuf, recvbuf, ccount, comms_blocking_->mem_partners_send_reduce_scatter_block[i], comms_blocking_->mem_partners_recv_reduce_scatter_block[i], (char ***)comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].small_mem2, (int**)comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, (char**)sendbufs, (char**)recvbufs);
      }
    } else {
#endif
#ifdef XPMEM
      if (!comms_blocking_->shmem_alt) {
        ext_mpi_sendrecvbuf_init_xpmem_blocking(comms_blocking_->xpmem_tree_root, comms_blocking_->mpi_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].num_sockets_per_node, (char*)sendbuf, recvbuf, ccount * comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].all_xpmem_id_permutated, comms_blocking_->mem_partners_send_reduce_scatter_block[i], comms_blocking_->mem_partners_recv_reduce_scatter_block[i], (char ***)comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].small_mem1, (int**)comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, (char**)sendbufs, (char**)recvbufs);
      } else {
        ext_mpi_sendrecvbuf_init_xpmem_blocking(comms_blocking_->xpmem_tree_root, comms_blocking_->mpi_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].num_sockets_per_node, (char*)sendbuf, recvbuf, ccount * comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].all_xpmem_id_permutated, comms_blocking_->mem_partners_send_reduce_scatter_block[i], comms_blocking_->mem_partners_recv_reduce_scatter_block[i], (char ***)comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].small_mem2, (int**)comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, (char**)sendbufs, (char**)recvbufs);
      }
#endif
#ifdef GPU_ENABLED
    }
#endif
  } else {
    sendbufs[0] = (char*)sendbuf;
    recvbufs[0] = recvbuf;
  }
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
    if (!comms_blocking_->shmem_alt) {
      ext_mpi_exec_blocking(comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].comm_code, 1, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].num_sockets_per_node, (void **)comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].mem_gpu1, sendbufs, recvbufs, ((recvcount - 1) / comms_blocking_->padding_factor_reduce_scatter_block_blocking[i] + 1) * type_size, reduction_op, INT_MAX, comms_blocking_->p_dev_temp);
    } else {
      ext_mpi_exec_blocking(comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].comm_code, 1, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].num_sockets_per_node, (void **)comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].mem_gpu2, sendbufs, recvbufs, ((recvcount - 1) / comms_blocking_->padding_factor_reduce_scatter_block_blocking[i] + 1) * type_size, reduction_op, INT_MAX, comms_blocking_->p_dev_temp);
    }
  } else {
#endif
    if (!comms_blocking_->shmem_alt) {
      ext_mpi_exec_blocking(comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].comm_code, 1, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].num_sockets_per_node, (void **)comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].mem1, sendbufs, recvbufs, ((recvcount - 1) / comms_blocking_->padding_factor_reduce_scatter_block_blocking[i] + 1) * type_size, reduction_op, INT_MAX, comms_blocking_->p_dev_temp);
    } else {
      ext_mpi_exec_blocking(comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].comm_code, 1, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].num_sockets_per_node, (void **)comms_blocking_->comm_property_reduce_scatter_block[collective_subtype][i].mem2, sendbufs, recvbufs, ((recvcount - 1) / comms_blocking_->padding_factor_reduce_scatter_block_blocking[i] + 1) * type_size, reduction_op, INT_MAX, comms_blocking_->p_dev_temp);
    }
#ifdef GPU_ENABLED
  }
#endif
  comms_blocking_->shmem_alt = 1 - comms_blocking_->shmem_alt;
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
